using EspBarcode.Controller.Web.E2ETests.Support;
using Microsoft.Playwright;
using Reqnroll;

namespace EspBarcode.Controller.Web.E2ETests.StepDefinitions;

[Binding]
public class GatewayStepDefinitions(TestWorld world)
{
    private IPage Page => world.Page;

    [When("I put the first device into gateway mode")]
    [Given("I put the first device into gateway mode")]
    public async Task WhenIPutFirstDeviceIntoGatewayMode()
    {
        await Page.GoToSpaAsync("devices", "Devices");
        var card = Page.Locator("[data-testid=device-card]").First;
        await card.Locator("[data-testid=enter-gateway]").ClickAsync();
        // The v1 "gateway" handshake + v2 "system.hello" round trip is async; wait for the badge
        // instead of a fixed delay so this step doesn't race ahead of the in-flight negotiation.
        await Assertions.Expect(card.GetByText("Gateway relay"))
            .ToBeVisibleAsync(new LocatorAssertionsToBeVisibleOptions { Timeout = 10000 });
    }

    [When("I open the Gateway page")]
    public async Task WhenIOpenGatewayPage() => await Page.GoToSpaAsync("gateway", "Gateway");

    [When("I set the relay data to {string} for that device")]
    public async Task WhenISetRelayData(string data)
    {
        await Page.Locator("[data-testid=gateway-data]").First.FillAsync(data);
    }

    [When("I click Generate via Relay for that device")]
    public async Task WhenIClickGenerateViaRelay()
    {
        await Page.Locator("[data-testid=gateway-generate]").First.ClickAsync();
        await Task.Delay(300);
    }

    [Then("the Gateway page shows a negotiated control session for that device")]
    public async Task ThenGatewayShowsNegotiatedSession()
    {
        await Page.GoToSpaAsync("gateway", "Gateway");
        await Assertions.Expect(Page.Locator("[data-testid=gateway-card]").First).ToContainTextAsync("control session #");
    }

    [Then("the relay reports a successful generate result")]
    public async Task ThenRelayReportsSuccess()
        => await Assertions.Expect(Page.Locator("[data-testid=gateway-card]").First).ToContainTextAsync("Relayed:");

    [When("I click \"Ping for Clients\" for that device")]
    public async Task WhenIClickPingForClients()
    {
        await Page.Locator("[data-testid=gateway-ping-now]").First.ClickAsync();
        await Task.Delay(1500); // the app itself waits ~1.2s before re-listing peers after a ping
    }

    [When("I click \"Refresh Peers\" for that device")]
    public async Task WhenIClickRefreshPeers()
    {
        await Page.Locator("[data-testid=gateway-refresh-peers]").First.ClickAsync();
        await Task.Delay(300);
    }

    [Then("the Gateway page lists a discovered peer for that device")]
    public async Task ThenGatewayListsDiscoveredPeer()
        => await Assertions.Expect(Page.Locator("[data-testid=gateway-peer-row]").First).ToBeVisibleAsync();

    [Then("the Gateway page shows no discovered peers for that device")]
    public async Task ThenGatewayShowsNoPeers()
        => await Assertions.Expect(Page.Locator("[data-testid=gateway-peers]").First).ToContainTextAsync("No ESP-NOW peers seen yet");

    [Given("I put the first device into gateway mode with a trusted device {string}")]
    public async Task GivenFirstDeviceHasTrustedDevice(string fingerprint)
    {
        await world.ConfigureFakeDevicesAsync(authorizedCount: 2, trustedPeers:
            [new { fingerprint, mac = "AA:BB:CC:DD:EE:02", route_id = 1, paired_at_ms = 0, label = "" }]);
        // ConfigureFakeDevicesAsync reloads the app, which drops the connections the Background's
        // "I reconnect known devices" step made -- reconnect again before entering gateway mode, or
        // no [data-testid=device-card] exists yet for WhenIPutFirstDeviceIntoGatewayMode to target.
        await Page.GoToSpaAsync("devices", "Devices");
        await Page.Locator("[data-testid=reconnect-authorized]").ClickAsync();
        await Task.Delay(300);
        await WhenIPutFirstDeviceIntoGatewayMode();
    }

    [When("I click \"Pair new device\" for that device")]
    public async Task WhenIClickPairNewDevice()
    {
        await Page.Locator("[data-testid=gateway-pair-new-device]").First.ClickAsync();
        await Task.Delay(1200); // matches the poll timer's ~1s cadence (Task 10 Step 3)
    }

    [When("I click \"Refresh Trust List\" for that device")]
    public async Task WhenIClickRefreshTrustList()
    {
        await Page.Locator("[data-testid=gateway-refresh-trust]").First.ClickAsync();
        await Task.Delay(300);
    }

    [When("I click \"Forget\" for that device")]
    public async Task WhenIClickForget()
    {
        await Page.Locator("[data-testid=gateway-trust-forget]").First.ClickAsync();
        await Task.Delay(300);
    }

    [Then("the Gateway page shows no trusted devices for that device")]
    public async Task ThenGatewayShowsNoTrustedDevices()
        => await Assertions.Expect(Page.Locator("[data-testid=gateway-trust-empty]").First).ToBeVisibleAsync();

    [Then("the Gateway page shows a pairing code for that device")]
    public async Task ThenGatewayShowsPairingCode()
        => await Assertions.Expect(Page.Locator("[data-testid=gateway-pairing-code]").First).ToBeVisibleAsync();
}
