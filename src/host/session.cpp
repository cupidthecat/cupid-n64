#include "session_internal.hpp"

namespace cupid::host {

Session::Session() : impl_(std::make_unique<Impl>()) {}
Session::~Session() = default;

u64 Session::load(Options options, bool paused, std::filesystem::path storage_root) {
    return impl_->enqueue(Impl::Action::Load, std::move(options), paused, std::move(storage_root));
}

u64 Session::pause(bool paused) {
    return impl_->enqueue(Impl::Action::Pause, {}, paused);
}

u64 Session::reset() {
    return impl_->enqueue(Impl::Action::Reset);
}

u64 Session::stop(bool discard_unsaved) {
    return impl_->enqueue(Impl::Action::Stop, {}, discard_unsaved);
}

void Session::set_input(const std::array<ControllerState, 4>& input) {
    std::lock_guard lock(impl_->mutex);
    impl_->input = input;
}

SessionStatus Session::status() const {
    std::lock_guard lock(impl_->mutex);
    return impl_->snapshot;
}

SessionOutput Session::take_output() {
    std::lock_guard lock(impl_->mutex);
    SessionOutput result{impl_->snapshot.output_epoch, std::move(impl_->latest_video),
                         impl_->audio_buffer.take()};
    impl_->latest_video.reset();
    return result;
}

bool Session::wait(u64 request, std::chrono::milliseconds timeout) const {
    if (request == 0)
        return false;
    std::unique_lock lock(impl_->mutex);
    return impl_->changed.wait_for(lock, timeout,
                                   [&] { return impl_->snapshot.completed_request >= request; });
}

u64 Session::Impl::enqueue(Action action, Options selected, bool value, std::filesystem::path storage_root) {
    std::lock_guard lock(mutex);
    if (commands.size() >= 32)
        return 0;
    const u64 request = next_request++;
    commands.push_back({action, request, std::move(selected), value, std::move(storage_root)});
    changed.notify_all();
    return request;
}

} // namespace cupid::host
