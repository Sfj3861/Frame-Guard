#include <algorithm>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

struct InputEvent {
    unsigned long long frame;
    double timestamp_ms;
    std::string label;
};

static void print_report(const std::vector<InputEvent>& events,
                         double refresh_rate_hz,
                         std::size_t window_size) {
    const double frame_ms = 1000.0 / refresh_rate_hz;

    std::cout << "Frame-Guard | synthetic timing report\n"
              << "-------------------------------------\n"
              << "Mode: offline simulation (no game hooks)\n"
              << "Simulated refresh rate: " << refresh_rate_hz << " Hz\n"
              << "Frame duration: " << std::fixed << std::setprecision(3)
              << frame_ms << " ms\n"
              << "Event window: " << window_size << " events\n\n";

    if (events.empty()) {
        std::cout << "No events to analyze.\n";
        return;
    }

    std::cout << std::left << std::setw(8) << "Frame"
              << std::setw(16) << "Time (ms)"
              << "Event\n";
    std::cout << "-------------------------------------\n";

    const std::size_t start =
        events.size() > window_size ? events.size() - window_size : 0;
    for (std::size_t i = start; i < events.size(); ++i) {
        std::cout << std::left << std::setw(8) << events[i].frame
                  << std::setw(16) << std::fixed << std::setprecision(3)
                  << events[i].timestamp_ms
                  << events[i].label << '\n';
    }

    double total_interval = 0.0;
    std::size_t intervals = 0;
    for (std::size_t i = 1; i < events.size(); ++i) {
        total_interval += events[i].timestamp_ms - events[i - 1].timestamp_ms;
        ++intervals;
    }

    if (intervals > 0) {
        std::cout << "\nMean interval: " << std::fixed << std::setprecision(3)
                  << total_interval / static_cast<double>(intervals)
                  << " ms\n";
    }
    std::cout << "\nNote: all events are synthetic sample data.\n";
}

int main(int argc, char** argv) {
    double refresh_rate_hz = 144.0;
    std::size_t window_size = 16;

    if (argc > 1) {
        try {
            refresh_rate_hz = std::stod(argv[1]);
        } catch (...) {
            std::cerr << "Usage: frame-guard [refresh-rate-hz] [event-window]\n";
            return 2;
        }
    }
    if (argc > 2) {
        try {
            const unsigned long parsed = std::stoul(argv[2]);
            window_size = static_cast<std::size_t>(parsed);
        } catch (...) {
            std::cerr << "Usage: frame-guard [refresh-rate-hz] [event-window]\n";
            return 2;
        }
    }

    if (refresh_rate_hz <= 0.0 || refresh_rate_hz > 1000.0) {
        std::cerr << "Refresh rate must be greater than 0 and at most 1000 Hz.\n";
        return 2;
    }
    if (window_size == 0 || window_size > 16) {
        std::cerr << "Event window must be between 1 and 16.\n";
        return 2;
    }

    const double frame_ms = 1000.0 / refresh_rate_hz;
    const std::vector<InputEvent> sample_events = {
        {100, 100 * frame_ms, "sample input A"},
        {103, 103 * frame_ms, "sample input B"},
        {104, 104 * frame_ms, "sample input C"},
        {110, 110 * frame_ms, "sample input D"},
        {111, 111 * frame_ms, "sample input E"},
    };

    print_report(sample_events, refresh_rate_hz, window_size);
    return 0;
}
