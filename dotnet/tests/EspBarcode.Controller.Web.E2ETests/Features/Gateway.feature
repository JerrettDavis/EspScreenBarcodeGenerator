Feature: Gateway relay control
  As a lab operator
  I want to switch a board into USB<->ESP-NOW gateway mode and reach the far side
  So that I can control a display that only has ESP-NOW connectivity

  Background:
    Given the controller app is open with 2 authorized fake ESP screens
    And I reconnect known devices

  Scenario: Entering gateway mode negotiates an EspLink v2 session
    When I put the first device into gateway mode
    Then the Gateway page shows a negotiated control session for that device

  Scenario: Generating a barcode through the gateway relay
    Given I put the first device into gateway mode
    When I open the Gateway page
    And I set the relay data to "GATEWAY-001" for that device
    And I click Generate via Relay for that device
    Then the relay reports a successful generate result

  Scenario: Pinging for prospective clients discovers a peer
    Given I put the first device into gateway mode
    When I open the Gateway page
    And I click "Ping for Clients" for that device
    Then the Gateway page lists a discovered peer for that device

  Scenario: Refreshing the peers list shows no peers before any ping
    Given I put the first device into gateway mode
    When I open the Gateway page
    And I click "Refresh Peers" for that device
    Then the Gateway page shows no discovered peers for that device

  Scenario: Trust card starts with no paired devices
    Given I put the first device into gateway mode
    When I open the Gateway page
    Then the Gateway page shows no trusted devices for that device

  Scenario: Pairing a new device shows the on-device approval code
    Given I put the first device into gateway mode
    When I open the Gateway page
    And I click "Ping for Clients" for that device
    And I click "Pair new device" for that device
    Then the Gateway page shows a pairing code for that device

  Scenario: Forgetting a trusted device removes it from the list
    Given I put the first device into gateway mode with a trusted device "A3F9-21C4"
    When I open the Gateway page
    And I click "Refresh Trust List" for that device
    And I click "Forget" for that device
    Then the Gateway page shows no trusted devices for that device
