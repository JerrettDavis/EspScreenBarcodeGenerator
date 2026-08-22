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
