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
