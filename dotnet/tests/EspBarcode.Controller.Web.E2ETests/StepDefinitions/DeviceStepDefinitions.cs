using EspBarcode.Controller.Web.E2ETests.Support;
using Microsoft.Playwright;
using Reqnroll;
using Xunit;

namespace EspBarcode.Controller.Web.E2ETests.StepDefinitions;

[Binding]
public class DeviceStepDefinitions(TestWorld world)
{
    private IPage Page => world.Page;

    [Given("the controller app is open with {int} authorized fake ESP screens")]
    public async Task GivenAppOpenWithAuthorizedDevices(int count) => await world.ConfigureFakeDevicesAsync(count);

    [Given("no devices have been authorized yet")]
    public async Task GivenNoDevicesAuthorized() => await world.ConfigureFakeDevicesAsync(authorizedCount: 0, unauthorizedCount: 1);

    [Given("I have not manually reconnected any devices")]
    public void GivenNotReconnected() { /* documents intent; nothing to set up */ }

    [When("I reconnect known devices")]
    [Given("I reconnect known devices")]
    public async Task WhenIReconnectKnownDevices()
    {
        await GotoDevicesAsync();
        await Page.Locator("[data-testid=reconnect-authorized]").ClickAsync();
        await Task.Delay(300); // fake device responses are near-instant, but let the UI settle
    }

    [When("I connect a new device")]
    public async Task WhenIConnectNewDevice()
    {
        await GotoDevicesAsync();
        await Page.Locator("[data-testid=connect-new-device]").ClickAsync();
        await Task.Delay(300);
    }

    [When("I rename the first device to {string}")]
    public async Task WhenIRenameFirstDevice(string name)
    {
        await GotoDevicesAsync();
        var input = Page.Locator("[data-testid=device-nickname]").First;
        await input.FillAsync(name);
        await input.DispatchEventAsync("change");
    }

    [When("I disconnect the first device")]
    public async Task WhenIDisconnectFirstDevice()
    {
        await GotoDevicesAsync();
        await Page.Locator("[data-testid=device-card]").First.GetByText("Disconnect", new LocatorGetByTextOptions { Exact = true }).ClickAsync();
        await Task.Delay(200);
    }

    [When("I refresh the first device")]
    public async Task WhenIRefreshFirstDevice()
    {
        await GotoDevicesAsync();
        await Page.Locator("[data-testid=device-card]").First.GetByText("Refresh", new LocatorGetByTextOptions { Exact = true }).ClickAsync();
        await Task.Delay(200);
    }

    [Then("the device list shows a gateway link status of {string} for the first device")]
    public async Task ThenGatewayLinkStatusShows(string expected)
    {
        await GotoDevicesAsync();
        var status = Page.Locator("[data-testid=device-card]").First.Locator("[data-testid=gateway-link-status]");
        await Assertions.Expect(status).ToContainTextAsync(expected, new LocatorAssertionsToContainTextOptions { IgnoreCase = true });
    }

    [Then("the device list shows {int} connected device(s)")]
    public async Task ThenDeviceListShowsCount(int count)
    {
        await GotoDevicesAsync();
        await Assertions.Expect(Page.Locator("[data-testid=device-card]")).ToHaveCountAsync(count);
    }

    [Then("each device reports its firmware over the \"hello\" handshake")]
    public async Task ThenEachDeviceReportsFirmware()
    {
        await GotoDevicesAsync();
        var cards = Page.Locator("[data-testid=device-card]");
        var count = await cards.CountAsync();
        Assert.True(count > 0, "expected at least one device card");
        for (var i = 0; i < count; i++)
        {
            await Assertions.Expect(cards.Nth(i)).ToContainTextAsync(TestWorld.DefaultFirmware);
        }
    }

    [Then("the device list shows a device named {string}")]
    public async Task ThenDeviceListShowsNamedDevice(string name)
    {
        await GotoDevicesAsync();
        var input = Page.Locator("[data-testid=device-nickname]");
        var count = await input.CountAsync();
        for (var i = 0; i < count; i++)
        {
            if (await input.Nth(i).InputValueAsync() == name) return;
        }

        Assert.Fail($"no device named '{name}' found");
    }

    [When("I resize the controller to a phone viewport")]
    public async Task WhenIResizeToPhone() => await Page.SetViewportSizeAsync(390, 844);

    [Then("the PWA manifest is linked")]
    public async Task ThenManifestIsLinked()
    {
        await Assertions.Expect(Page.Locator("link[rel=manifest]")).ToHaveAttributeAsync("href", "manifest.webmanifest");
        var response = await Page.APIRequest.GetAsync(AppServer.BaseUrl + "/manifest.webmanifest");
        Assert.True(response.Ok, "expected the PWA manifest to be served");
        var manifest = await response.TextAsync();
        Assert.Contains("\"sizes\": \"192x192\"", manifest);
        Assert.Contains("\"sizes\": \"512x512\"", manifest);
    }

    [Then("navigation is docked to the bottom of the phone viewport")]
    public async Task ThenNavigationIsBottomDocked()
    {
        var nav = Page.Locator(".esb-sidebar"); var box = await nav.BoundingBoxAsync();
        Assert.NotNull(box); Assert.InRange(box!.Y + box.Height, 842, 846);
    }

    private async Task GotoDevicesAsync() => await Page.GoToSpaAsync("devices", "Devices");
}
