#include "session_internal.hpp"

#include "cupid/host/hardware.hpp"
#include "cupid/host/preferences.hpp"
#include "cupid/host/storage.hpp"

#include <exception>
#include <stdexcept>

namespace cupid::host {

bool Session::Impl::flush(std::string& error) {
    return !system || flush_persistent_storage(*system, options, error);
}

void Session::Impl::load_machine(Options selected, bool paused, const std::filesystem::path& storage_root) {
    running = false;
    clear_outputs(false);
    {
        std::lock_guard lock(mutex);
        snapshot.state = SessionState::Loading;
        snapshot.error.clear();
    }

    std::string error;
    auto candidate = create_system(selected, error);
    if (!candidate ||
        (!storage_root.empty() && !prepare_storage_paths(*candidate, selected, storage_root, error)) ||
        !validate_storage_paths(selected, error) || !flush(error)) {
        fail(std::move(error));
        return;
    }
    // A reload may use the same save file that the previous machine just wrote.
    if (!load_persistent_storage(*candidate, selected, error)) {
        fail(std::move(error));
        return;
    }
    system = std::move(candidate);
    options = std::move(selected);
    fields = dma_samples = 0;
    clear_outputs(true);
    attach_outputs();
    running = !paused;
    restart_pacing();
    {
        std::lock_guard lock(mutex);
        snapshot.cartridge = path_text(options.cartridge);
        snapshot.hardware = describe_hardware(*system);
        snapshot.error.clear();
    }
    publish_status();
}

void Session::Impl::execute(Command command) {
    try {
        switch (command.action) {
        case Action::Load:
            load_machine(std::move(command.options), command.value, command.storage_root);
            break;
        case Action::Pause:
            if (system) {
                if (!command.value && system->cpu.frozen)
                    throw std::runtime_error("The CPU is stalled. Stop or reload the cartridge to continue.");
                running = !command.value;
                clear_outputs(false);
                restart_pacing();
                {
                    std::lock_guard lock(mutex);
                    snapshot.error.clear();
                }
                publish_status();
            }
            break;
        case Action::Reset:
            if (system) {
                system->set_reset_button(true);
                system->set_reset_button(false);
                clear_outputs(false);
                restart_pacing();
            }
            break;
        case Action::Stop: {
            running = false;
            clear_outputs(false);
            std::string error;
            if (!command.value && !flush(error)) {
                fail(std::move(error));
                break;
            }
            system.reset();
            clear_outputs(true);
            {
                std::lock_guard lock(mutex);
                const auto request = snapshot.completed_request;
                const auto epoch = snapshot.output_epoch;
                snapshot = {};
                snapshot.completed_request = request;
                snapshot.output_epoch = epoch;
            }
            break;
        }
        }
    } catch (const std::exception& error) {
        fail(error.what());
    }
    {
        std::lock_guard lock(mutex);
        snapshot.completed_request = command.request;
    }
    changed.notify_all();
}

void Session::Impl::fail(std::string error) {
    running = false;
    clear_outputs(false);
    std::lock_guard lock(mutex);
    snapshot.has_machine = system != nullptr;
    snapshot.state = system ? SessionState::Paused : SessionState::Empty;
    snapshot.error = error.empty() ? "The requested operation could not be completed." : std::move(error);
}

} // namespace cupid::host
