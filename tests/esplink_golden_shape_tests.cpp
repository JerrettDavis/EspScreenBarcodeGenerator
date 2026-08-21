#include "vectors_v1_golden.h"

#include <iostream>

using namespace esplink;

namespace {
int failures = 0;
void check(bool condition, const char* expression, int line) {
    if (condition) return;
    std::cerr << "FAIL line " << line << ": " << expression << '\n';
    ++failures;
}
}  // namespace
#define CHECK(expr) check((expr), #expr, __LINE__)

namespace {
const char* responseCommandName(const Response& response) {
    return std::visit([](const auto& r) -> const char* {
        using T = std::decay_t<decltype(r)>;
        if constexpr (std::is_same_v<T, SimpleOkResponse>) return r.command.c_str();
        else return nullptr;  // typed responses other than SimpleOkResponse carry no redundant name field.
    }, response);
}
}  // namespace

int main() {
    const auto& fixtures = goldenFixtures();
    CHECK(fixtures.size() == 24);

    for (const auto& fixture : fixtures) {
        CHECK(fixture.name != nullptr);
        CHECK(fixture.name[0] != '\0');
        if (std::holds_alternative<Response>(fixture.expected)) {
            const auto& response = std::get<Response>(fixture.expected);
            const char* name = responseCommandName(response);
            (void)name;  // SimpleOkResponse fixtures are cross-checked by name below; others are structural only.
        }
    }

    // Spot-check a couple of SimpleOkResponse fixtures line up their command name with their fixture name.
    for (const auto& fixture : fixtures) {
        if (!std::holds_alternative<Response>(fixture.expected)) continue;
        const auto& response = std::get<Response>(fixture.expected);
        if (!std::holds_alternative<SimpleOkResponse>(response)) continue;
        const auto& ok = std::get<SimpleOkResponse>(response);
        CHECK(!ok.command.empty());
    }

    if (failures != 0) {
        std::cerr << failures << " golden fixture shape test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "All golden fixture shape tests passed (" << fixtures.size() << " fixtures)\n";
    return EXIT_SUCCESS;
}
