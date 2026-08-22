using EspBarcode.Controller.Web.E2ETests.Support;
using Microsoft.Playwright;
using Reqnroll;

namespace EspBarcode.Controller.Web.E2ETests.StepDefinitions;

[Binding]
public class LibraryStepDefinitions(TestWorld world)
{
    private IPage Page => world.Page;

    [Given("a library item named {string} exists")]
    public async Task GivenLibraryItemExists(string name)
    {
        await Page.GoToSpaAsync("generator", "Generator");
        await Page.Locator("[data-testid=generator-data]").FillAsync("SEED-DATA");
        await Page.Locator("[data-testid=generator-saveas]").FillAsync(name);
        await Page.Locator("[data-testid=save-to-library]").ClickAsync();
        await Page.Locator("[data-testid=generator-message]").WaitForAsync();
    }

    [When("I open the Library page")]
    public async Task WhenIOpenLibraryPage() => await Page.GoToSpaAsync("library", "Storage");

    [When("I select the first device in the on-device presets panel")]
    public async Task WhenISelectFirstDeviceInPresetsPanel()
    {
        var select = Page.Locator("[data-testid=preset-device-select]");
        var firstValue = await select.Locator("option").Nth(1).GetAttributeAsync("value") ?? "";
        await select.SelectOptionAsync(firstValue);
    }

    [When("I save the currently displayed symbol as preset {string}")]
    public async Task WhenISaveCurrentlyDisplayedAsPreset(string name)
    {
        await Page.Locator("[data-testid=preset-save-name]").FillAsync(name);
        await Page.Locator("[data-testid=preset-save-button]").ClickAsync();
        await Task.Delay(200);
    }

    [When("I delete the library item named {string}")]
    public async Task WhenIDeleteLibraryItem(string name)
    {
        var row = Page.Locator("[data-testid=library-row]", new PageLocatorOptions { HasText = name });
        await row.Locator("[data-testid=library-delete]").ClickAsync();
    }

    [Then("the on-device preset list contains {string}")]
    public async Task ThenOnDevicePresetListContains(string name)
        => await Assertions.Expect(Page.Locator("[data-testid=preset-table]")).ToContainTextAsync(name);

    [Then("the library does not contain an item named {string}")]
    public async Task ThenLibraryDoesNotContain(string name)
    {
        var row = Page.Locator("[data-testid=library-row]", new PageLocatorOptions { HasText = name });
        await Assertions.Expect(row).ToHaveCountAsync(0);
    }
}
