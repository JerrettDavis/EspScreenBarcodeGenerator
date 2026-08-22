using EspBarcode.Controller.Web.E2ETests.Support;
using Microsoft.Playwright;
using Reqnroll;

namespace EspBarcode.Controller.Web.E2ETests.StepDefinitions;

[Binding]
public class AutomationStepDefinitions(TestWorld world)
{
    private IPage Page => world.Page;

    [When("I enable Full Auto Mode")]
    public async Task WhenIEnableFullAutoMode()
    {
        await Page.GoToSpaAsync("automation", "Full Auto Mode");
        await Page.Locator("[data-testid=automation-enabled]").EvaluateAsync(
            "el => { el.checked = true; el.dispatchEvent(new Event('change', { bubbles: true })); }");
        await Page.Locator("[data-testid=automation-save]").ClickAsync();
    }

    [When("I enable Full Auto Mode with playlist rotation of {string} every {int} second(s)")]
    public async Task WhenIEnableFullAutoModeWithRotation(string itemName, int intervalSeconds)
    {
        await Page.GoToSpaAsync("automation", "Full Auto Mode");
        await Page.Locator("[data-testid=automation-enabled]").EvaluateAsync(
            "el => { el.checked = true; el.dispatchEvent(new Event('change', { bubbles: true })); }");
        await Page.Locator("[data-testid=automation-rotate-enabled]").CheckAsync();
        await Page.Locator("[data-testid=automation-rotate-interval]").FillAsync(intervalSeconds.ToString());
        await Page.Locator($"[data-testid=automation-playlist-item][data-item-name=\"{itemName}\"] input")
            .CheckAsync();
        await Page.Locator("[data-testid=automation-save]").ClickAsync();
    }

    [Then("the device list eventually shows {int} connected device(s)")]
    public async Task ThenDeviceListEventuallyShowsCount(int count)
    {
        await Page.GoToSpaAsync("devices", "Devices");
        await Assertions.Expect(Page.Locator("[data-testid=device-card]"))
            .ToHaveCountAsync(count, new LocatorAssertionsToHaveCountOptions { Timeout = 15000 });
    }

    [Then("the top bar shows the {string} badge")]
    public async Task ThenTopBarShowsBadge(string text)
        => await Assertions.Expect(Page.GetByText(text).First).ToBeVisibleAsync();

    [Then("the automation page eventually reports a rotation to {string}")]
    public async Task ThenAutomationPageEventuallyReportsRotation(string itemName)
    {
        await Page.GoToSpaAsync("automation", "Full Auto Mode");
        await Assertions.Expect(Page.Locator("[data-testid=automation-summary]"))
            .ToContainTextAsync($"Rotated to '{itemName}'", new LocatorAssertionsToContainTextOptions { Timeout = 15000 });
    }
}
