# Live-TPL
Acts as a usecase for Recursive-Tag-Replacement-Engine, allowing the editing of files and live templating previews inside of neovim.

## Brief

Live TUI now utilises shared memory to access the contents of the active neovim buffer. This content is then modified by Recursive-Tag-Replacement-Engine in the live TUI preview, where it can be saved as a snapshot as well.