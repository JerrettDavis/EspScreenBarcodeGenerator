Feature: Mobile wireless control
  As a lab operator using a phone
  I want to select nearby Bluetooth screens and import barcode photos
  So that I can update one or more displays without a cable

  Scenario: Importing a QR photo prefills a clean request
    Given the controller app is open with fake Bluetooth screens
    When I upload a barcode photo
    Then the wireless barcode type is "Qr"
    And the wireless barcode data is "PHOTO-QR-001"
