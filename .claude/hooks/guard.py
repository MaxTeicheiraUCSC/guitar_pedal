#!/usr/bin/env python3
"""PreToolUse guard: confine this Claude Code session to the guitar_pedal repo.

Enforced by the harness before every tool call (see .claude/settings.json), so it holds
regardless of what an email or any other untrusted input persuades the model to try.

  * File tools: paths must be inside the repo or the session scratchpad.
  * Bash: only build/test/git/gh/read-only utilities; no paths outside the repo; no
    network/exfil binaries; no git credential/remote/config changes; no force pushes.
  * Gmail: only search/get/reply; reply must go to the thread's original sender
    (no to/cc/bcc override, no reply-all) and the body is scanned for secrets, absolute
    paths, and third-party addresses.
  * Everything else that reaches outside the repo (other connectors, web fetch) is denied
    in settings.json permissions.
Output: JSON permissionDecision deny with a reason, or nothing (allow).
"""
import json, os, re, sys

REPO = os.path.realpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
SCRATCH_PREFIX = "/private/tmp/claude-501/-Users-maxteicheira-Documents-Claude-guitar-pedal/"
TOOL_BIN_PREFIXES = ("/Applications/KiCad/", "/opt/homebrew/", "/usr/bin/", "/usr/local/bin/", "/bin/")

def deny(reason):
    print(json.dumps({"hookSpecificOutput": {"hookEventName": "PreToolUse", "permissionDecision": "deny",
                      "permissionDecisionReason": "[repo guard] " + reason}}))
    sys.exit(0)

def inside_repo(path):
    p = os.path.realpath(os.path.expanduser(path)) if not os.path.isabs(path) else os.path.realpath(path)
    if not os.path.isabs(path):
        p = os.path.realpath(os.path.join(os.environ.get("CLAUDE_PROJECT_DIR", REPO), path))
    return p == REPO or p.startswith(REPO + os.sep) or p.startswith(SCRATCH_PREFIX) or p.startswith("/private" + SCRATCH_PREFIX)

# ---------------------------------------------------------------- file tools
def check_file_tool(name, inp):
    for key in ("file_path", "path", "notebook_path", "directory"):
        v = inp.get(key)
        if isinstance(v, str) and v and not inside_repo(v):
            deny(f"{name} outside the repo: {v}")
    if name in ("Glob", "Grep"):
        pat = inp.get("pattern", "")
        if pat.startswith("/") or pat.startswith("~") or ".." in pat:
            deny(f"{name} pattern must stay inside the repo: {pat}")
        if "path" not in inp and os.path.realpath(os.getcwd()) not in (REPO,) and not os.getcwd().startswith(REPO):
            deny(f"{name} without a path while cwd is outside the repo")

# ---------------------------------------------------------------- bash
ALLOWED_CMDS = {
    "cmake", "make", "ctest", "ninja", "python3", "python", "git", "gh", "ngspice",
    "ls", "cat", "head", "tail", "grep", "egrep", "sed", "awk", "find", "wc", "diff", "sort", "uniq", "tr", "cut",
    "echo", "printf", "mkdir", "cp", "mv", "rm", "touch", "chmod", "cd", "pwd", "test", "true", "false", "xargs", "tee",
    "time", "date", "stat", "du", "file", "unzip", "zip", "tar", "sleep", "pkill", "pgrep", "basename", "dirname", "realpath",
    "ffmpeg", "sips", "qlmanage", "lldb", "kicad-cli", "otool", "nm", "strings", "md5", "shasum", "jq", "seq", "env", "export",
}
DENIED_CMDS = {"curl", "wget", "nc", "ncat", "netcat", "ssh", "scp", "sftp", "rsync", "ftp", "telnet", "mail", "sendmail",
               "osascript", "open", "security", "defaults", "launchctl", "sudo", "su", "printenv", "crontab", "dscl", "pbcopy",
               "screencapture", "say", "brew", "pip", "pip3", "conda", "npm", "npx", "node", "uv", "uvx"}
BAD_PATH_RE = re.compile(r"(~|\$HOME|\$\{HOME\}|/Users/(?!maxteicheira/Documents/Claude/guitar_pedal(/|\s|$|['\"]))|/etc/|/private/etc|/var/(?!folders)|/Library/|/System/|\.ssh|\.aws|\.gnupg|\.netrc|\.claude(?!/hooks|/settings)|\.config/|Keychain|/\.\./)")
GIT_DENY_RE = re.compile(
    r"\bgit\s+("
    r"config(?!\s+(--get|--list|-l\b))"                     # reading config is fine; writing is not
    r"|remote\s+(add|remove|rm|rename|set-url|set-head|set-branches|prune)"   # `git remote -v/show` is read-only
    r"|credential|filter-branch|update-ref|--exec-path|-c\s+"
    r"|push\s+.*(--force|-f\b|\+)"
    r")")
GH_ALLOW_RE = re.compile(r"\bgh\s+(run|release|repo\s+view|auth\s+status)\b")

HEREDOC_RE = re.compile(r"<<-?\s*['\"]?(\w+)['\"]?[^\n]*\n.*?\n\1[ \t]*(?=\n|$)", re.S)
def first_words(command):
    # split a shell command into pipeline/sequence segments and return each segment's command word;
    # heredoc bodies are data, not commands (the path/exfil regex still scans the full text)
    command = HEREDOC_RE.sub("<<HEREDOC", command)
    command = re.sub(r'"(?:[^"\\]|\\.)*"|\'[^\']*\'', '""', command)   # quoted strings are arguments, not commands
    segs = re.split(r"\|\||&&|\||;|\n|\(|\)|`|\$\(", command)
    out = []
    for s in segs:
        s = s.strip()
        if not s: continue
        # strip env assignments and leading redirects
        toks = s.split()
        while toks and re.match(r"^[A-Za-z_][A-Za-z0-9_]*=", toks[0]): toks.pop(0)
        if toks: out.append(toks[0])
    return out

def check_bash(inp):
    cmd = inp.get("command", "")
    m = BAD_PATH_RE.search(cmd)
    if m: deny(f"command references a location outside the repo: '{m.group(0)}'")
    for w in first_words(cmd):
        base = os.path.basename(w)
        if base in DENIED_CMDS: deny(f"'{base}' is not allowed in this session (network/system tool)")
        if w.startswith("./") or w.startswith(REPO) or w.startswith("build") or w.startswith("hardware") or w.startswith("emulator"): continue
        if w.startswith("/") and not w.startswith(TOOL_BIN_PREFIXES): deny(f"absolute executable outside allowed tool dirs: {w}")
        if not w.startswith("/") and base not in ALLOWED_CMDS: deny(f"'{base}' is not on the command allowlist")
    if GIT_DENY_RE.search(cmd): deny("git config/remote/credential changes and force pushes are blocked")
    if re.search(r"\bgh\b", cmd) and not GH_ALLOW_RE.search(cmd): deny("gh is limited to run/release/repo view/auth status")
    if re.search(r"\bgit\s+push\b", cmd) and not re.search(r"\bgit\s+push\s+(-q\s+)?origin\s+(main|v\d+\.\d+\.\d+)\b", cmd):
        deny("git push is limited to 'origin main' or a version tag")

# ---------------------------------------------------------------- gmail
SECRET_RE = re.compile(r"(gh[pousr]_[A-Za-z0-9]{20,}|AKIA[0-9A-Z]{16}|sk-[A-Za-z0-9]{20,}|-----BEGIN|xox[bap]-|api[_-]?key|password|passwd|secret|token|Bearer\s|/Users/|/private/|ssh-rsa|ssh-ed25519)", re.I)
def check_gmail(name, inp):
    short = name.replace("mcp__claude_ai_Gmail__", "")
    if short not in ("search_threads", "get_thread", "get_message", "reply", "list_labels"):
        deny(f"Gmail {short} is not allowed; only search/get/reply")
    if short == "reply":
        if inp.get("to") or inp.get("cc") or inp.get("bcc"): deny("reply may not override recipients (goes to the original sender only)")
        if inp.get("replyAll"): deny("reply-all is not allowed")
        body = (inp.get("body") or "") + " " + (inp.get("htmlBody") or "")
        m = SECRET_RE.search(body)
        if m: deny(f"reply body contains something that looks like a secret or local path: '{m.group(0)}'")
        if len(body) > 6000: deny("reply body too long")
        if re.search(r"[\w.+-]+@[\w-]+\.[\w.]+", body): deny("reply body may not contain email addresses")

def main():
    try: data = json.load(sys.stdin)
    except Exception: return
    name = data.get("tool_name", ""); inp = data.get("tool_input") or {}
    if name in ("Read", "Edit", "Write", "MultiEdit", "NotebookEdit", "Glob", "Grep", "LS"): check_file_tool(name, inp)
    elif name == "Bash": check_bash(inp)
    elif name.startswith("mcp__claude_ai_Gmail__"): check_gmail(name, inp)
    elif name.startswith("mcp__"): deny(f"connector {name} is not allowed in this project")
    elif name in ("WebFetch", "WebSearch"): deny(f"{name} is disabled in this project (no outbound channels besides git/gh/Gmail reply)")

if __name__ == "__main__":
    main()
