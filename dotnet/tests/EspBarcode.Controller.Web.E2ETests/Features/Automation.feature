Feature: Full Auto Mode
  As a lab operator
  I want the controller to run unattended
  So that reconnects and demo rotations happen without me clicking through the app

  Background:
    Given the controller app is open with 2 authorized fake ESP screens

  Scenario: Enabling Full Auto Mode reconnects known devices automatically
    Given I have not manually reconnected any devices
    When I enable Full Auto Mode
    Then the device list eventually shows 2 connected devices
    And the top bar shows the "Full Auto Mode" badge

  Scenario: Full Auto Mode rotates a playlist across target devices
    Given I reconnect known devices
    And a library item named "Rotator Item" exists
    When I enable Full Auto Mode with playlist rotation of "Rotator Item" every 1 second
    Then the automation page eventually reports a rotation to "Rotator Item"
