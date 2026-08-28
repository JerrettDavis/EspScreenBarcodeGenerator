Feature: Barcode generator
  As a lab operator
  I want to generate a barcode and push it to one or more screens
  So that I can put a new label on a display without touching the device

  Background:
    Given the controller app is open with 2 authorized fake ESP screens
    And I reconnect known devices

  Scenario: Generating and pushing a QR code to a device
    When I set the generator type to "Qr" and data to "LAB-TEST-001"
    And I select the first device as a generator target
    And I click Generate & Push
    Then the generator reports success
    And a live preview is rendered

  Scenario: Saving a spec to the local library
    When I set the generator type to "Code128" and data to "LOT-2026-00042"
    And I save the generator spec to the library as "Lot Sample"
    Then the library contains an item named "Lot Sample"

  Scenario: Sending a barcode to two nearby Bluetooth screens
    Given I connect two Bluetooth screens from Devices
    When I set the generator type to "Qr" and data to "MOBILE-LAB-001"
    And I select the first Bluetooth generator target
    And I select the second Bluetooth generator target
    And I click Generate & Push
    Then the generator reports it pushed to 2 device(s)

  Scenario: Sending through a wired ESP-NOW gateway to itself
    Given I put the first device into gateway mode
    When I set the generator type to "Qr" and data to "GATEWAY-DIRECT-001"
    And I select the gateway generator target
    And I click Generate & Push
    Then the generator reports it pushed to 1 device(s)

  Scenario: Addressing one paired screen through its gateway route
    Given a gateway has a paired screen on route 7
    When I set the generator type to "Qr" and data to "ROUTED-SCREEN-007"
    And I refresh paired gateway screens on the generator
    And I select the first paired gateway generator target
    And I click Generate & Push
    Then the generator reports it pushed to 1 device(s)

  Scenario: Importing a QR photo prefills a clean request
    When I upload a barcode photo to the generator
    Then the generator barcode type is "Qr"
    And the generator data is "PHOTO-QR-001"

  Scenario: Result preview falls back to a text summary for a Bluetooth-only target
    Given I connect a Bluetooth screen from Devices
    When I set the generator type to "Qr" and data to "BLE-PREVIEW-001"
    And I select the first Bluetooth generator target
    And I click Generate & Push
    Then the generator reports it pushed to 1 device(s)
    And a text result summary is shown instead of a live preview
