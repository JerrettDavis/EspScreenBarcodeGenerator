using EspBarcode.Controller.Web.E2ETests.Support;
using Microsoft.Playwright;
using Reqnroll;

namespace EspBarcode.Controller.Web.E2ETests.StepDefinitions;

[Binding]
public sealed class BluetoothStepDefinitions(TestWorld world)
{
    private IPage Page => world.Page;

    [When("I connect a Bluetooth screen from Devices")]
    [Given("I connect a Bluetooth screen from Devices")]
    public async Task WhenIConnectOneBluetoothScreenFromDevices()
    {
        await Page.GoToSpaAsync("devices", "Devices");
        await Page.Locator("[data-testid=connect-bluetooth]").ClickAsync();
        await Assertions.Expect(Page.Locator("[data-testid=bluetooth-device-card]")).ToHaveCountAsync(1);
    }

    [When("I connect two Bluetooth screens from Devices")]
    [Given("I connect two Bluetooth screens from Devices")]
    public async Task WhenIConnectTwoBluetoothScreensFromDevices()
    {
        await Page.GoToSpaAsync("devices", "Devices");
        var button = Page.Locator("[data-testid=connect-bluetooth]");
        await button.ClickAsync();
        await Assertions.Expect(Page.Locator("[data-testid=bluetooth-device-card]")).ToHaveCountAsync(1);
        await button.ClickAsync();
        await Assertions.Expect(Page.Locator("[data-testid=bluetooth-device-card]")).ToHaveCountAsync(2);
    }

    [When("I disconnect the first Bluetooth screen")]
    public async Task WhenIDisconnectFirstBluetoothScreen()
    {
        await Page.Locator("[data-testid=bluetooth-device-card]").First
            .GetByText("Disconnect", new LocatorGetByTextOptions { Exact = true }).ClickAsync();
    }

    [Then("the device list shows {int} connected Bluetooth screen(s)")]
    public async Task ThenBluetoothDeviceListShowsCount(int count)
        => await Assertions.Expect(Page.Locator("[data-testid=bluetooth-device-card]")).ToHaveCountAsync(count);
}
