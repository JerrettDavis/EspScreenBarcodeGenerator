Feature: Theme
  As a lab operator
  I want the browser control panel to match the on-device light/dark theme
  So that the whole product family looks consistent

  Background:
    Given the controller app is open with 2 authorized fake ESP screens

  Scenario: Default theme is dark
    Then the app theme is "dark"

  Scenario: Switching to light theme persists across reloads
    When I switch the app theme to "light"
    Then the app theme is "light"
    When I reload the app
    Then the app theme is "light"
