Feature: Device connection
  As a lab operator
  I want to connect ESP screens to the browser control panel over Web Serial
  So that I can monitor and control them without any native software

  Background:
    Given the controller app is open with 2 authorized fake ESP screens

  Scenario: Reconnecting previously authorized devices
    When I reconnect known devices
    Then the device list shows 2 connected devices
    And each device reports its firmware over the "hello" handshake

  Scenario: Connecting a new device via the browser picker
    Given no devices have been authorized yet
    When I connect a new device
    Then the device list shows 1 connected device

  Scenario: Renaming a connected device
    When I reconnect known devices
    And I rename the first device to "Receiving Dock"
    Then the device list shows a device named "Receiving Dock"

  Scenario: Disconnecting a device
    When I reconnect known devices
    And I disconnect the first device
    Then the device list shows 1 connected device

  Scenario: A plain client device shows its ESP-NOW gateway-discovery status
    When I reconnect known devices
    And I refresh the first device
    Then the device list shows a gateway link status of "searching" for the first device

  Scenario: The controller is installable and uses mobile navigation
    When I resize the controller to a phone viewport
    Then the PWA manifest is linked
    And navigation is docked to the bottom of the phone viewport

  Scenario: Connecting Bluetooth screens from the Devices page
    When I connect two Bluetooth screens from Devices
    Then the device list shows 2 connected Bluetooth screens

  Scenario: Disconnecting a Bluetooth screen
    When I connect two Bluetooth screens from Devices
    And I disconnect the first Bluetooth screen
    Then the device list shows 1 connected Bluetooth screen
