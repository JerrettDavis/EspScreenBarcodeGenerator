Feature: Mobile wireless control
  As a lab operator using a phone
  I want to select nearby Bluetooth screens and import barcode photos
  So that I can update one or more displays without a cable

  Scenario: Sending a barcode to two nearby Bluetooth screens
    Given the controller app is open with fake Bluetooth screens
    When I connect two nearby Bluetooth screens
    And I enter wireless barcode data "MOBILE-LAB-001"
    And I send the wireless barcode
    Then the wireless controller reports it sent to 2 screens
    And both Bluetooth screen writes overlapped

  Scenario: Importing a QR photo prefills a clean request
    Given the controller app is open with fake Bluetooth screens
    When I upload a barcode photo
    Then the wireless barcode type is "Qr"
    And the wireless barcode data is "PHOTO-QR-001"

  Scenario: Sending through a wired ESP-NOW gateway from the mobile workflow
    Given the controller app is open with 2 authorized fake ESP screens
    And I reconnect known devices
    And I put the first device into gateway mode
    When I open the unified wireless controller
    And I select the first wired or gateway target
    And I enter wireless barcode data "MOBILE-GATEWAY-001"
    And I send the wireless barcode
    Then the wireless controller reports it sent to 1 screen

  Scenario: Addressing one paired screen through its gateway route
    Given the controller app is open with fake Bluetooth screens
    And a gateway has a paired screen on route 7
    When I open the unified wireless controller
    And I refresh paired gateway screens
    And I select the first paired gateway screen
    And I enter wireless barcode data "ROUTED-SCREEN-007"
    And I send the wireless barcode
    Then the wireless controller reports it sent to 1 screen
