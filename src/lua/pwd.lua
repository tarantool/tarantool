local ffi   = require('ffi')
local errno = require('errno')

-- {{{ Definitions

-- GID_T, UID_T and TIME_T are, essentially, `integer types`.
-- http://pubs.opengroup.org/onlinepubs/009695399/basedefs/sys/types.h.html
ffi.cdef[[
    typedef int uid_t;
    typedef int gid_t;
    typedef long time_t;
]]

-- POSIX demands to have three fields in struct group:
-- http://pubs.opengroup.org/onlinepubs/009695399/basedefs/grp.h.html
-- char   *gr_name The name of the group.
-- gid_t   gr_gid  Numerical group ID.
-- char  **gr_mem  Pointer to a null-terminated array of character pointers to
--                 member names.
--
-- So we'll extract only them.
ffi.cdef[[
    struct group {
        char    *gr_name;    /* group name */
        char    *gr_passwd;  /* group password */
        gid_t    gr_gid;     /* group id */
        char   **gr_mem;     /* group members */
    };
]]

-- POSIX demands to have five fields in struct group:
-- char    *pw_name   User's login name.
-- uid_t    pw_uid    Numerical user ID.
-- gid_t    pw_gid    Numerical group ID.
-- char    *pw_dir    Initial working directory.
-- char    *pw_shell  Program to use as shell.
--
-- So we'll extract only them.
if ffi.os == 'OSX' or ffi.os == 'BSD' then
    ffi.cdef[[
        struct passwd {
            char    *pw_name;    /* user name */
            char    *pw_passwd;  /* encrypted password */
            uid_t    pw_uid;     /* user uid */
            gid_t    pw_gid;     /* user gid */
            time_t   pw_change;  /* password change time */
            char    *pw_class;   /* user access class */
            char    *pw_gecos;   /* Honeywell login info */
            char    *pw_dir;     /* home directory */
            char    *pw_shell;   /* default shell */
            time_t   pw_expire;  /* account expiration */
            int      pw_fields;  /* internal: fields filled in */
        };
    ]]
else
    ffi.cdef[[
        struct passwd {
            char *pw_name;   /* username */
            char *pw_passwd; /* user password */
            int   pw_uid;    /* user ID */
            int   pw_gid;    /* group ID */
            char *pw_gecos;  /* user information */
            char *pw_dir;    /* home directory */
            char *pw_shell;  /* shell program */
        };
    ]]
end

ffi.cdef[[
    uid_t          getuid();
    struct passwd *getpwuid(uid_t uid);
    struct passwd *getpwnam(const char *login);
    void           endpwent();
    struct passwd *getpwent();
    void           setpwent();

    gid_t          getgid();
    struct group  *getgrgid(gid_t gid);
    struct group  *getgrnam(const char *group);
    struct group  *getgrent();
    void           endgrent();
    void           setgrent();
]]

-- }}}

-- {{{ Error handling

local pwgr_errstr = "%s failed [errno %d]: %s"

-- Use it in the following way: set errno to zero, call a passwd /
-- group function, then call this function to check whether there
-- was an error.
local function pwgrcheck(func_name, pwgr)
    if pwgr ~= nil then
        return
    end
    if errno() == 0 then
        return
    end
    error(pwgr_errstr:format(func_name, errno(), errno.strerror()))
end

-- }}}

-- {{{ Call passwd / group database

local function _getpw(uid)
    local pw = nil
    errno(0)
    if type(uid) == 'number' then
        pw = ffi.C.getpwuid(uid)
    elseif type(uid) == 'string' then
        pw = ffi.C.getpwnam(uid)
    else
        error("Bad type of uid (expected 'string'/'number')")
    end
    pwgrcheck('_getpw', pw)
    return pw
end

local function _getgr(gid)
    local gr = nil
    errno(0)
    if type(gid) == 'number' then
        gr = ffi.C.getgrgid(gid)
    elseif type(gid) == 'string' then
        gr = ffi.C.getgrnam(gid)
    else
        error("Bad type of gid (expected 'string'/'number')")
    end
    pwgrcheck('_getgr', gr)
    return gr
end

-- }}}

-- {{{ Serialize passwd / group structures to tables

local function grtotable(gr)
    local gr_mem, group_members = gr.gr_mem, {}
    local i = 0
    while true do
        local member = gr_mem[i]
        if member == nil then
            break
        end
        table.insert(group_members, ffi.string(member))
        i = i + 1
    end
    return {
        id      = tonumber(gr.gr_gid),
        name    = ffi.string(gr.gr_name),
        members = group_members,
    }
end

-- gr is optional
local function pwtotable(pw, gr)
    local user = {
        name    = ffi.string(pw.pw_name),
        id      = tonumber(pw.pw_uid),
        workdir = ffi.string(pw.pw_dir),
        shell   = ffi.string(pw.pw_shell),
    }
    if gr ~= nil then
        user.group = grtotable(gr)
    end
    return user
end

-- }}}

-- {{{ Public API functions

local function getgr(gid)
    if gid == nil then
        gid = tonumber(ffi.C.getgid())
    end
    local gr = _getgr(gid)
    if gr == nil then
        return nil
    end
    return grtotable(gr)
end

local function getpw(uid)
    if uid == nil then
        uid = tonumber(ffi.C.getuid())
    end
    local pw = _getpw(uid)
    if pw == nil then
        return nil
    end
    local gr = _getgr(pw.pw_gid) -- can be nil
    return pwtotable(pw, gr)
end

--
-- systemd v209 sets errno to ENOENT in
-- _nss_systemd_getpwent_r and _nss_systemd_getgrent_r
-- when there are no more entries left to enumerate.
--
-- This is a bug which has been fixed later in
-- systemd code but we have to deal with buggy
-- environment thus ignore such error if appears.
--
-- See the reference for details
-- https://github.com/systemd/systemd/issues/9585
--
-- The issue affects getpwent/getgrent calls only
-- thus provide own wrappings to keep workaround
-- in one place and do not affect any other calls
-- where ENOENT might become a valid error case.
--
-- Initially we observed this issue on Fedora 29
-- when a password database is traversed for the
-- first time.
local function getpwent()
    errno(0)
    local pw = ffi.C.getpwent()
    if pw == nil and errno() == errno.ENOENT then
        errno(0)
    end
    return pw
end

local function getgrent()
    errno(0)
    local gr = ffi.C.getgrent()
    if gr == nil and errno() == errno.ENOENT then
        errno(0)
    end
    return gr
end

local function getpwall()
    ffi.C.setpwent()
    local pws = {}
    -- Note: Don't call _getpw() during the loop: it leads to drop
    -- of a getpwent() current entry to a first one on CentOS 6
    -- and FreeBSD 12.
    while true do
        local pw = getpwent()
        if pw == nil then
            pwgrcheck('getpwall', pw)
            break
        end
        local gr = _getgr(pw.pw_gid) -- can be nil
        table.insert(pws, pwtotable(pw, gr))
    end
    ffi.C.endpwent()
    return pws
end

local function getgrall()
    ffi.C.setgrent()
    local grs = {}
    -- Note: Don't call _getgr() during the loop: it leads to drop
    -- of a getgrent() current entry to a first one on CentOS 6
    -- and FreeBSD 12.
    while true do
        local gr = getgrent()
        if gr == nil then
            pwgrcheck('getgrall', gr)
            break
        end
        table.insert(grs, grtotable(gr))
    end
    ffi.C.endgrent()
    return grs
end

-- }}}

return {
    getpw = getpw,
    getgr = getgr,
    getpwall = getpwall,
    getgrall = getgrall,
}
