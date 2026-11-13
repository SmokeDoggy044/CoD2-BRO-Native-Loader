#include "title_calls.h"

namespace xbox {

namespace {
TitleCalls g_title_calls;
}

void set_title_calls(const TitleCalls& calls) { g_title_calls = calls; }
const TitleCalls& title_calls() { return g_title_calls; }

}  // namespace xbox
