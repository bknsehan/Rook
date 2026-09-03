use zed_extension_api::{self as zed, Command, Extension, LanguageServerId, Result, Worktree};

struct RookExtension;

impl Extension for RookExtension {
    fn new() -> Self {
        RookExtension
    }

    fn language_server_command(
        &mut self,
        _language_server_id: &LanguageServerId,
        worktree: &Worktree,
    ) -> Result<Command> {
        let path = worktree.which("rook-lsp");
        let command = if let Some(p) = path {
            p
        } else {
            "/home/bknsehan/bin/Rook/bin/rook-lsp".to_string()
        };

        Ok(Command {
            command,
            args: vec![],
            env: Default::default(),
        })
    }
}

zed::register_extension!(RookExtension);
