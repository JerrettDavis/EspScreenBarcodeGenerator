using EspBarcode.Controller.Web.E2ETests.Support;
using Microsoft.Playwright;
using Reqnroll;

namespace EspBarcode.Controller.Web.E2ETests.StepDefinitions;

[Binding]
public class ThemeStepDefinitions(TestWorld world)
{
    private IPage Page => world.Page;

    [When(@"I switch the app theme to ""(.*)""")]
    public async Task WhenISwitchAppTheme(string theme)
    {
        await Page.GoToSpaAsync("settings", "Settings");
        var testId = theme == "light" ? "theme-light" : "theme-dark";
        await Page.Locator($"[data-testid={testId}]").ClickAsync();
    }

    [When("I reload the app")]
    public async Task WhenIReloadTheApp()
    {
        await Page.ReloadAsync();
        await Page.GetByText("ESP Barcode Control").First.WaitForAsync();
    }

    [Then(@"the app theme is ""(.*)""")]
    public async Task ThenAppThemeIs(string theme)
        => await Assertions.Expect(Page.Locator(".esb-app")).ToHaveAttributeAsync("data-theme", theme);
}
