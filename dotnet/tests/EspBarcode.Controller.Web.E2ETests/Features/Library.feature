Feature: Storage (library and on-device presets)
  As a lab operator
  I want browser-side barcode storage and device-side preset control
  So that I can reuse specs without regenerating them by hand

  Background:
    Given the controller app is open with 2 authorized fake ESP screens
    And I reconnect known devices

  Scenario: Saving current display as an on-device preset
    When I set the generator type to "Qr" and data to "LOT_SAMPLE"
    And I select the first device as a generator target
    And I click Generate & Push
    And I open the Library page
    And I select the first device in the on-device presets panel
    And I save the currently displayed symbol as preset "LOT_SAMPLE"
    Then the on-device preset list contains "LOT_SAMPLE"

  Scenario: Deleting a library item
    Given a library item named "Old Spec" exists
    When I open the Library page
    And I delete the library item named "Old Spec"
    Then the library does not contain an item named "Old Spec"
