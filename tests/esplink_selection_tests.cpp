#include "SelectionPolicy.h"

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

TransportCapabilities available(TransportKind kind, bool wireless = false, bool hw = false,
                                 bool ip = false, bool multi = false) {
    return TransportCapabilities{kind, CapabilityState::Available, wireless, hw, ip, multi};
}

void test_no_requirement_picks_highest_score_regardless_of_wireless() {
    SelectionPolicyConfig policy;
    policy.goal = OptimizationGoal::LowLatency;
    std::vector<TransportCapabilities> candidates = {
        available(TransportKind::UsbV2, /*wireless=*/false),
        available(TransportKind::Bluetooth, /*wireless=*/true),
    };
    auto decision = evaluateSelection(policy, candidates);
    CHECK(decision.selected.has_value());
    CHECK(*decision.selected == TransportKind::UsbV2);
}

void test_wireless_required_excludes_usb() {
    SelectionPolicyConfig policy;
    policy.goal = OptimizationGoal::LowLatency;
    policy.requirement.wirelessRequired = true;
    std::vector<TransportCapabilities> candidates = {
        available(TransportKind::UsbV2, false),
        available(TransportKind::Bluetooth, true),
    };
    auto decision = evaluateSelection(policy, candidates);
    CHECK(decision.selected.has_value());
    CHECK(*decision.selected == TransportKind::Bluetooth);
    CHECK(!decision.candidates[0].eligible);
    CHECK(decision.candidates[0].reason == std::string("wireless required"));
}

void test_deny_list_removes_a_candidate_even_if_best_scoring() {
    SelectionPolicyConfig policy;
    policy.goal = OptimizationGoal::HighThroughput;
    policy.requirement.wirelessRequired = true;
    policy.deny = {TransportKind::WifiDirect};
    std::vector<TransportCapabilities> candidates = {
        available(TransportKind::WifiDirect, true),
        available(TransportKind::Bluetooth, true),
    };
    auto decision = evaluateSelection(policy, candidates);
    CHECK(*decision.selected == TransportKind::Bluetooth);
    CHECK(decision.candidates[0].reason == std::string("denied by policy"));
}

void test_ip_interface_forbidden_excludes_wifi_direct() {
    SelectionPolicyConfig policy;
    policy.goal = OptimizationGoal::Balanced;
    policy.requirement.wirelessRequired = true;
    policy.requirement.ipInterface = SideEffectPermission::Forbidden;
    std::vector<TransportCapabilities> candidates = {
        available(TransportKind::WifiDirect, true, false, /*ip=*/true),
        available(TransportKind::EspNowGateway, true, /*hw=*/true, /*ip=*/false),
    };
    auto decision = evaluateSelection(policy, candidates);
    CHECK(*decision.selected == TransportKind::EspNowGateway);
}

void test_external_hardware_forbidden_excludes_gateway() {
    SelectionPolicyConfig policy;
    policy.goal = OptimizationGoal::Balanced;
    policy.requirement.wirelessRequired = true;
    policy.requirement.externalHardware = SideEffectPermission::Forbidden;
    std::vector<TransportCapabilities> candidates = {
        available(TransportKind::EspNowGateway, true, /*hw=*/true),
        available(TransportKind::Bluetooth, true, /*hw=*/false),
    };
    auto decision = evaluateSelection(policy, candidates);
    CHECK(*decision.selected == TransportKind::Bluetooth);
}

void test_all_candidates_ineligible_yields_no_selection_with_reasons() {
    SelectionPolicyConfig policy;
    std::vector<TransportCapabilities> candidates = {
        TransportCapabilities{TransportKind::Bluetooth, CapabilityState::CompiledOut},
        TransportCapabilities{TransportKind::WifiDirect, CapabilityState::Blocked},
    };
    auto decision = evaluateSelection(policy, candidates);
    CHECK(!decision.selected.has_value());
    CHECK(decision.candidates[0].reason == std::string("compiled out"));
    CHECK(decision.candidates[1].reason == std::string("blocked"));
}

void test_allow_list_restricts_to_named_kinds() {
    SelectionPolicyConfig policy;
    policy.allow = {TransportKind::UsbV2};
    std::vector<TransportCapabilities> candidates = {
        available(TransportKind::UsbV2),
        available(TransportKind::Bluetooth, true),
    };
    auto decision = evaluateSelection(policy, candidates);
    CHECK(*decision.selected == TransportKind::UsbV2);
    CHECK(decision.candidates[1].reason == std::string("not in allow list"));
}

void test_decision_is_deterministic_across_repeated_evaluation() {
    SelectionPolicyConfig policy;
    policy.goal = OptimizationGoal::Deterministic;
    std::vector<TransportCapabilities> candidates = {
        available(TransportKind::UsbV1),
        available(TransportKind::UsbV2),
    };
    auto first = evaluateSelection(policy, candidates);
    auto second = evaluateSelection(policy, candidates);
    CHECK(first.selected.has_value() && second.selected.has_value());
    CHECK(*first.selected == *second.selected);
    CHECK(*first.selected == TransportKind::UsbV2);
}

void test_fallback_never_forbids_even_before_a_session_exists() {
    CHECK(!isFallbackAllowed(FallbackPolicy::Never, FallbackContext{}));
}

void test_fallback_allowed_before_any_session_exists_under_any_other_policy() {
    CHECK(isFallbackAllowed(FallbackPolicy::UnavailableOnly, FallbackContext{}));
    CHECK(isFallbackAllowed(FallbackPolicy::ConnectFailure, FallbackContext{}));
    CHECK(isFallbackAllowed(FallbackPolicy::PreOperation, FallbackContext{}));
}

void test_fallback_forbidden_during_an_in_flight_transfer() {
    FallbackContext context;
    context.sessionEstablished = true;
    context.transferInFlight = true;
    CHECK(!isFallbackAllowed(FallbackPolicy::ConnectFailure, context));
}

void test_fallback_forbidden_for_an_unproven_mutation_even_under_connect_failure_policy() {
    FallbackContext context;
    context.sessionEstablished = true;
    context.mutationInFlight = true;
    context.operationProvenSafeToReplay = false;
    CHECK(!isFallbackAllowed(FallbackPolicy::ConnectFailure, context));
}

void test_fallback_allowed_for_a_proven_safe_mutation_under_connect_failure_policy() {
    FallbackContext context;
    context.sessionEstablished = true;
    context.mutationInFlight = true;
    context.operationProvenSafeToReplay = true;
    CHECK(isFallbackAllowed(FallbackPolicy::ConnectFailure, context));
}

}  // namespace

int main() {
    test_no_requirement_picks_highest_score_regardless_of_wireless();
    test_wireless_required_excludes_usb();
    test_deny_list_removes_a_candidate_even_if_best_scoring();
    test_ip_interface_forbidden_excludes_wifi_direct();
    test_external_hardware_forbidden_excludes_gateway();
    test_all_candidates_ineligible_yields_no_selection_with_reasons();
    test_allow_list_restricts_to_named_kinds();
    test_decision_is_deterministic_across_repeated_evaluation();
    test_fallback_never_forbids_even_before_a_session_exists();
    test_fallback_allowed_before_any_session_exists_under_any_other_policy();
    test_fallback_forbidden_during_an_in_flight_transfer();
    test_fallback_forbidden_for_an_unproven_mutation_even_under_connect_failure_policy();
    test_fallback_allowed_for_a_proven_safe_mutation_under_connect_failure_policy();
    if (failures != 0) {
        std::cerr << failures << " esplink selection test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "All esplink selection tests passed\n";
    return EXIT_SUCCESS;
}
