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

    [When("I open the unified wireless controller")]
    public async Task WhenIOpenUnifiedController() => await Page.GoToSpaAsync("wireless", "Nearby Screens");

    [When("I enter wireless barcode data {string}")]
    public async Task WhenIEnterData(string data) => await Page.Locator("[data-testid=wireless-data]").FillAsync(data);

    [When("I send the wireless barcode")]
    public async Task WhenISend() => await Page.Locator("[data-testid=wireless-send]").ClickAsync();

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
