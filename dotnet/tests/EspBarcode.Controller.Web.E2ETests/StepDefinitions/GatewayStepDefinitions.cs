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
}
