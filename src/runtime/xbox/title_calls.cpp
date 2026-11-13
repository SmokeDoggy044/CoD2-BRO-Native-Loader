#include "title_calls.h"

namespace xbox {

namespace {
TitleCalls g_title_calls;
EventSignal g_event_signal = nullptr;
IrqlCalls g_irql;
}

void set_title_calls(const TitleCalls& calls) { g_title_calls = calls; }
const TitleCalls& title_calls() { return g_title_calls; }

void set_event_signal(EventSignal signal) { g_event_signal = signal; }
void signal_title_event(uint32_t handle) {
    if (g_event_signal) g_event_signal(handle);
}

void set_irql_calls(const IrqlCalls& calls) { g_irql = calls; }
uint8_t title_raise_irql(uint8_t new_irql) { return g_irql.raise ? g_irql.raise(new_irql) : 0; }
void title_lower_irql(uint8_t old_irql) {
    if (g_irql.lower) g_irql.lower(old_irql);
}

}  // namespace xbox
