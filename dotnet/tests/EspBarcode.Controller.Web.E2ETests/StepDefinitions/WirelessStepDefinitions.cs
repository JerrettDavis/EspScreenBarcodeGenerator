using EspBarcode.Controller.Web.E2ETests.Support;
using Microsoft.Playwright;
using Reqnroll;

namespace EspBarcode.Controller.Web.E2ETests.StepDefinitions;

[Binding]
public sealed class WirelessStepDefinitions(TestWorld world)
{
    private IPage Page => world.Page;

    [Given("the controller app is open with fake Bluetooth screens")]
    public async Task GivenOpenWithBluetoothScreens() => await Page.GoToSpaAsync("wireless", "Nearby Screens");

    [Given("a gateway has a paired screen on route 7")]
    public async Task GivenGatewayHasRoutedScreen()
    {
        await world.ConfigureFakeDevicesAsync(2, trustedPeers:
            [new { fingerprint = "ROUTE-0007", mac = "AA:BB:CC:DD:EE:07", route_id = 7, paired_at_ms = 10, label = "Lab Screen 7" }],
            requiredRouteId: 7);
        await Page.GoToSpaAsync("devices", "Devices");
        await Page.Locator("[data-testid=reconnect-authorized]").ClickAsync();
        await Assertions.Expect(Page.Locator("[data-testid=device-card]")).ToHaveCountAsync(2);
        var firstCard = Page.Locator("[data-testid=device-card]").First;
        await firstCard.Locator("[data-testid=enter-gateway]").ClickAsync();
        await Assertions.Expect(firstCard.GetByText("Gateway relay")).ToBeVisibleAsync();
        await Page.GoToSpaAsync("gateway", "Gateway");
        await Assertions.Expect(Page.Locator("[data-testid=gateway-card]")).ToHaveCountAsync(1);
    }

    [When("I resize the controller to a phone viewport")]
    public async Task WhenIResizeToPhone() => await Page.SetViewportSizeAsync(390, 844);

    [Then("the PWA manifest is linked")]
    public async Task ThenManifestIsLinked()
    {
        await Assertions.Expect(Page.Locator("link[rel=manifest]")).ToHaveAttributeAsync("href", "manifest.webmanifest");
        var response = await Page.APIRequest.GetAsync(AppServer.BaseUrl + "/manifest.webmanifest");
        Xunit.Assert.True(response.Ok, "expected the PWA manifest to be served");
        var manifest = await response.TextAsync();
        Xunit.Assert.Contains("\"sizes\": \"192x192\"", manifest);
        Xunit.Assert.Contains("\"sizes\": \"512x512\"", manifest);
    }

    [Then("navigation is docked to the bottom of the phone viewport")]
    public async Task ThenNavigationIsBottomDocked()
    {
        var nav = Page.Locator(".esb-sidebar"); var box = await nav.BoundingBoxAsync();
        Xunit.Assert.NotNull(box); Xunit.Assert.InRange(box!.Y + box.Height, 842, 846);
    }

    [When("I open the unified wireless controller")]
    public async Task WhenIOpenUnifiedController() => await Page.GoToSpaAsync("wireless", "Nearby Screens");

    [When("I connect two nearby Bluetooth screens")]
    public async Task WhenIConnectTwoScreens()
    {
        var button = Page.Locator("[data-testid=connect-bluetooth]");
        await button.ClickAsync(); await Assertions.Expect(Page.Locator("[data-testid=bluetooth-device]")).ToHaveCountAsync(1);
        await button.ClickAsync(); await Assertions.Expect(Page.Locator("[data-testid=bluetooth-device]")).ToHaveCountAsync(2);
    }

    [When("I enter wireless barcode data {string}")]
    public async Task WhenIEnterData(string data) => await Page.Locator("[data-testid=wireless-data]").FillAsync(data);

    [When("I select the first wired or gateway target")]
    public async Task WhenISelectFirstWiredTarget() => await Page.Locator("[data-testid=wired-target] input[type=checkbox]").First.CheckAsync();

    [When("I refresh paired gateway screens")]
    public async Task WhenIRefreshPairedScreens() => await Page.Locator("[data-testid=refresh-gateway-targets]").ClickAsync();

    [When("I select the first paired gateway screen")]
    public async Task WhenISelectPairedScreen() => await Page.Locator("[data-testid=gateway-peer-target] input[type=checkbox]").First.CheckAsync();

    [When("I send the wireless barcode")]
    public async Task WhenISend() => await Page.Locator("[data-testid=wireless-send]").ClickAsync();

    [Then("the wireless controller reports it sent to 2 screens")]
    public async Task ThenSentToTwo() => await Assertions.Expect(Page.Locator("[data-testid=wireless-message]")).ToContainTextAsync("Sent to 2 screen(s)");

    [Then("the wireless controller reports it sent to 1 screen")]
    public async Task ThenSentToOne() => await Assertions.Expect(Page.Locator("[data-testid=wireless-message]")).ToContainTextAsync("Sent to 1 screen(s)");

    [When("I upload a barcode photo")]
    public async Task WhenIUploadPhoto()
    {
        await Page.Locator("[data-testid=barcode-photo]").SetInputFilesAsync(TestWorld.SampleImagePath);
        await Page.Locator("[data-testid=decode-photo]").ClickAsync();
    }

    [Then("the wireless barcode type is {string}")]
    public async Task ThenTypeIs(string type) => await Assertions.Expect(Page.Locator("[data-testid=wireless-type]")).ToHaveValueAsync(type);

    [Then("the wireless barcode data is {string}")]
    public async Task ThenDataIs(string data) => await Assertions.Expect(Page.Locator("[data-testid=wireless-data]")).ToHaveValueAsync(data);
}
