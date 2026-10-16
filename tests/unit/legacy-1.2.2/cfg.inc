/* azmod 1.2.2: guest/azmod/azmod.c, frozen as a test oracle; do not edit */
static int settings_write(const char *path, const char *buf, size_t len, int durable) {
    char tmp[512];
    if (snprintf(tmp, sizeof tmp, "%s.new", path) >= (int) sizeof tmp)
        return -1;
    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0)
        return -1;
    size_t off = 0;
    while (off < len) {
        ssize_t w = write(fd, buf + off, len - off);
        if (w < 0 && errno == EINTR)
            continue;
        if (w <= 0)
            break;
        off += (size_t) w;
    }
    int ok = off == len && (!durable || fsync(fd) == 0);
    if (close(fd) != 0)
        ok = 0;
    if (!ok || rename(tmp, path) != 0) {
        unlink(tmp);
        return -1;
    }
    if (durable) {
        char dir[512];
        snprintf(dir, sizeof dir, "%s", path);
        char *slash = strrchr(dir, '/');
        if (slash && slash != dir) {
            *slash = 0;
            int dfd = open(dir, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
            if (dfd >= 0) {
                fsync(dfd);
                close(dfd);
            }
        }
    }
    return 0;
}
static int cfg_line1_width(const char *buf, int size) {
    int n = 0;
    while (n < size - 1 && (buf[n] == '0' || buf[n] == '1'))
        n++;
    if (n >= AZMOD_CFG_MIN_WIDTH && (buf[n] == '\0' || buf[n] == '\n' || buf[n] == '\r'))
        return n;
    return 0;
}
static void load_config(azmod_state *s) {
    FILE *f = fopen(CONFIG_PATH, "re");
    if (!f)
        return;
    char buf[AZMOD_MOD_COUNT + 66] = {0}; /* room for a newer build's wider line (below) */
    int line1_ok = 0;
    int migrated = 0; /* line 1 validated at a narrower, older width (see below) */
    if (fgets(buf, sizeof buf, f)) {
        /* Self-heal: line 1 must be a run of '0'/'1' chars terminated by end of
         * line. A truncated or corrupt cfg (a lone "1" is a known trigger) would
         * otherwise leave the SOURCE-list mod menu blank. If it does not
         * validate, keep azmod_state_init's defaults, skip the later lines, and
         * rewrite a clean default below.
         *
         * Width: the file is positional (menu.h), so mods are only ever
         * appended. A shorter line from an older build (at least
         * AZMOD_CFG_MIN_WIDTH) still means mod i at char i: load it, leave the
         * newer mods at their defaults, and rewrite at the new width. A longer
         * line was written by a newer build; its first AZMOD_MOD_COUNT chars
         * still mean our mods, so a rollback keeps every setting. The extra
         * chars are dropped at the next save. */
        /* Narrower than any shipped build wrote (AZMOD_CFG_MIN_WIDTH) is corrupt. */
        int n = cfg_line1_width(buf, (int) sizeof buf);
        if (n) {
            line1_ok = 1;
            int k = n < AZMOD_MOD_COUNT ? n : AZMOD_MOD_COUNT;
            for (int i = 0; i < k; i++)
                s->mod_enabled[i] = (buf[i] == '1');
            migrated = (n < AZMOD_MOD_COUNT);
            /* FPS Overlay is a child of the OVERLAYS master (mod 14). A file older
             * than the master had FPS standing alone: keep it on screen by
             * switching the master on with it. */
            if (n <= AZMOD_ACTION_OVERLAYS && s->mod_enabled[AZMOD_ACTION_FPS])
                s->mod_enabled[AZMOD_ACTION_OVERLAYS] = true;
            /* A file older than the Playlist Editing toggle (mod 18) keeps a
             * hand-made flag instead of pledit_apply() deleting it. */
            if (n <= AZMOD_ACTION_PLEDIT &&
                access("/home/root/settings/azmod-plreorder.on", F_OK) == 0)
                s->mod_enabled[AZMOD_ACTION_PLEDIT] = true;
            /* A file older than the Remote Stems row (mod 24) gets its default (ON). */
            if (n <= AZMOD_ACTION_REMOTESTEMS)
                s->mod_enabled[AZMOD_ACTION_REMOTESTEMS] = true;
            /* 60 FPS UI (mod 19) replaced the hand flags for the row cache and the
             * slow-drag fix: a file older than the toggle keeps them. */
            if (n <= AZMOD_ACTION_UI60UI &&
                (access("/home/root/settings/azmod-rowcache.on", F_OK) == 0 ||
                 access("/home/root/settings/azmod-touchaccum.on", F_OK) == 0))
                s->mod_enabled[AZMOD_ACTION_UI60UI] = true;
        }
    }
    /* Line 2 (optional): pad-mode color picker indices, one per button
     * (-1 = native). Absent -> defaults from state_init. Only trusted when
     * line 1 validated; otherwise the file is suspect. */
    if (line1_ok) {
        char cbuf[64];
        if (fgets(cbuf, sizeof cbuf, f)) {
            int v[AZMOD_PADMODE_COUNT], w[AZMOD_PADMODE_COUNT];
            for (int j = 0; j < AZMOD_PADMODE_COUNT; j++)
                v[j] = w[j] = -1;
            int master = 1; /* 5th field; absent on old configs -> ON */
            /* Fields 6-9: the secondary-page picks, appended so an older build
             * reads its first five unchanged; absent -> native. */
            int got = sscanf(cbuf, "%d %d %d %d %d %d %d %d %d", &v[0], &v[1], &v[2], &v[3],
                             &master, &w[0], &w[1], &w[2], &w[3]);
            for (int j = 0; j < AZMOD_PADMODE_COUNT; j++) {
                s->padmode_color[j] =
                    (int8_t) ((v[j] >= 0 && v[j] < AZMOD_PAD_PALETTE_COUNT) ? v[j] : -1);
                s->padmode_color2[j] =
                    (int8_t) ((got >= 6 + j && w[j] >= 0 && w[j] < AZMOD_PAD_PALETTE_COUNT) ? w[j]
                                                                                            : -1);
            }
            s->pad_colors_on = (got >= 5) ? (master != 0) : true;
        }
        /* Line 3 (optional): pin mask (decimal bitfield). Absent -> 0, no pins. */
        char pbuf[32];
        if (fgets(pbuf, sizeof pbuf, f)) {
            unsigned long long pm = 0;
            if (sscanf(pbuf, "%llu", &pm) == 1)
                s->pinned_mask = (uint64_t) (pm & AZMOD_MOD_MASK);
        }
    }
    fclose(f);
#ifdef AZMOD_RELEASE
    /* Release builds force Beat Sync on whatever the cfg says. Its row is hidden
     * (menu.c RELEASE_HIDDEN); pinning after the parse means the file can never
     * override it. azmod_state_init pins the same for the no-cfg early return above. */
    s->mod_enabled[AZMOD_ACTION_DRUMSYNC] = true;
#endif
    s->mod_enabled[AZMOD_ACTION_RTSTEMS] =
        false; /* mod 23: retired slot, always off */
    s->mod_enabled[AZMOD_ACTION_RETIRED12] = false; /* retired slots, always off */
    s->mod_enabled[AZMOD_ACTION_RETIRED26] = false;
    if (!line1_ok) {
        dbg("cfg: azmod.cfg failed validation -- resetting to a clean default (self-heal)\n");
        save_config(s); /* state stayed at azmod_state_init defaults; rewrite them */
    } else if (migrated) {
        /* Rewrite at the new width now, not on the first input event: nothing
         * drains config_dirty until then, and every boot would re-migrate. */
        dbg("cfg: migrated an older azmod.cfg to %d mods (appended mods keep their defaults)\n",
            AZMOD_MOD_COUNT);
        save_config(s);
    }
    /* 60 FPS on at boot arms the frame-clock halving for this session (paints
     * 32 -> 64/s, main thread at 71% of one core). The tmpfs flag dies on
     * reboot, so a boot with the toggle off (or a corrupt cfg) never arms it,
     * and turning the toggle off disarms mid-session because starttimer_hook
     * checks g_ui60_on on every fire. */
    if (ui60_clock_wanted(s)) {
        f = fopen("/tmp/azmod-disp60", "we"); /* state, not a log: never gated */
        if (f)
            fclose(f);
        __atomic_store_n(&g_disp60_armed, 1, __ATOMIC_RELAXED);
    }
    /* Flag files on p7 can outlive the cfg that produced them (hand-created, or
     * written by an older build), so the cfg is re-asserted as the authority at
     * every boot. */
    pledit_apply(s);
}
static void real_save_config(const azmod_state *s) {
    char buf[64 + AZMOD_MOD_COUNT + 24 * AZMOD_PADMODE_COUNT];
    size_t n = 0;
    for (int i = 0; i < AZMOD_MOD_COUNT; i++)
        buf[n++] = s->mod_enabled[i] ? '1' : '0';
    buf[n++] = '\n';
    for (int j = 0; j < AZMOD_PADMODE_COUNT; j++) /* line 2: pad colors */
        n += (size_t) snprintf(buf + n, sizeof buf - n, "%d ", s->padmode_color[j]);
    n += (size_t) snprintf(buf + n, sizeof buf - n, "%d", s->pad_colors_on ? 1 : 0); /* master */
    for (int j = 0; j < AZMOD_PADMODE_COUNT; j++) /* ...then the secondary pages */
        n += (size_t) snprintf(buf + n, sizeof buf - n, " %d", s->padmode_color2[j]);
    n += (size_t) snprintf(buf + n, sizeof buf - n, "\n%llu\n",
                           (unsigned long long) s->pinned_mask); /* line 3: pin mask */
    if (n < sizeof buf)
        settings_write(CONFIG_PATH, buf, n, 1);
}
