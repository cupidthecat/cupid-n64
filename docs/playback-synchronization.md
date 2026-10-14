# Playback synchronization

Audio and video synchronization are independent. Audio synchronization is on by default; video synchronization is off. The playback loop runs continuously and does not add a wall-clock frame limiter.

On Windows, audio fills a stereo queue and waits for the playback device's readiness event when the queue reaches the device buffer size. A ready event drains as many queued frames as the device can accept. With audio synchronization off, playback checks the event without waiting and discards the oldest excess frames when the device is full. Device-default latency uses twice the shared device period; an explicit latency can request a larger buffer.

Video synchronization enables the display swap interval. Both switches can be enabled, or both can be disabled. When both are disabled, emulation runs as fast as the host allows. An unavailable audio device does not enable a separate timer limiter.

Fast-forward temporarily disables both waits and restores the selected synchronization settings when it ends. Muting still submits silent audio, so audio synchronization continues to control playback speed. Pausing clears queued audio. Interrupted playback and stalled devices release audio waits so shutdown and device recovery can finish.

Existing synchronization selections remain readable. New settings store the audio and video switches separately.
