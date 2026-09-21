use zed_extension_api::{self as zed, Command, Extension, LanguageServerId, Result, Worktree};

struct RookExtension;

fn candidate_paths() -> Vec<String> {
    let mut out = Vec::new();
    // Explicit override for users with non-standard installs.
    if let Ok(p) = std::env::var("ROOK_LSP_PATH") {
        if !p.trim().is_empty() {
            out.push(p);
        }
    }
    // Default user install prefix used by install.sh.
    if let Ok(home) = std::env::var("HOME") {
        if !home.is_empty() {
            out.push(format!("{home}/bin/Rook/bin/rook-lsp"));
            out.push(format!("{home}/bin/rook-lsp"));
            out.push(format!("{home}/.local/bin/rook-lsp"));
        }
    }
    out.push("/usr/local/bin/rook-lsp".to_string());
    out.push("/usr/bin/rook-lsp".to_string());
    out
}

impl Extension for RookExtension {
    fn new() -> Self {
        RookExtension
    }

    fn language_server_command(
        &mut self,
        _language_server_id: &LanguageServerId,
        worktree: &Worktree,
    ) -> Result<Command> {
        // 1. Prefer whatever is on the worktree PATH (project-local installs).
        if let Some(p) = worktree.which("rook-lsp") {
            return Ok(Command {
                command: p,
                args: vec![],
                env: Default::default(),
            });
        }
        // 2. Try well-known install locations (GUI-launched Zed often has a
        //    minimal PATH that misses ~/bin).
        for p in candidate_paths() {
            if std::path::Path::new(&p).is_file() {
                return Ok(Command {
                    command: p,
                    args: vec![],
                    env: Default::default(),
                });
            }
        }
        // 3. Fall back to `rokade lsp` (same server, different entrypoint).
        if let Some(rokade) = worktree.which("rokade") {
            return Ok(Command {
                command: rokade,
                args: vec!["lsp".to_string()],
                env: Default::default(),
            });
        }
        // 4. Last resort: rely on PATH resolution and let Zed surface the error.
        Ok(Command {
            command: "rook-lsp".to_string(),
            args: vec![],
            env: Default::default(),
        })
    }
}

zed::register_extension!(RookExtension);
