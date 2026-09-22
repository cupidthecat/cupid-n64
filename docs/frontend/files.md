# Native file dialogs

The desktop frontend uses SDL's asynchronous native open-file dialog for media
and storage paths. `FileDialogs::open` starts one request and returns immediately.
The frontend polls `pending()` to avoid opening a second native dialog and consumes
completed work with `take_result()`.
A completed selection must be consumed before another request can open. This
keeps a second callback from overwriting an unread result or its routing tag.

Each result carries its `FileKind` and caller-supplied tag so one dialog service
can route cartridge, PIF firmware, Transfer Pak cartridge, and Controller Pak
selections back to the control that requested them. A successful selection stores
the UTF-8 path in `std::filesystem::path`. Cancellation sets `canceled` without an
error. SDL failures are copied into `error` while the callback is active.

The SDL filter arrays have static storage because SDL requires them to remain
valid until its callback. The callback also copies the selected UTF-8 string
before returning because SDL owns and frees the file list. Callback state is
heap-owned independently of the frontend object, so a native dialog may complete
after the controller or its parent window has been destroyed without dereferencing
either object. A mutex protects request state because SDL may invoke the callback
from a different thread.

The default launcher always calls `SDL_ShowOpenFileDialog` with single-selection
mode and no parent window. SDL 3.4.16 retains the supplied window pointer for its
native dialog thread; omitting that pointer lets the application window be
destroyed independently of an outstanding dialog. The chooser is therefore a
separate native window. The application pauses emulation and releases held
inputs while it is open.

Tests may inject a launcher to complete callbacks synchronously or from a
worker thread without displaying operating-system UI. A launcher must transfer
the request to exactly one callback, just as the SDL API does.
