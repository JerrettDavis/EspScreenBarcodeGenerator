using EspBarcode.Controller.Web.E2ETests.Support;
using Microsoft.Playwright;
using Reqnroll;

namespace EspBarcode.Controller.Web.E2ETests.StepDefinitions;

[Binding]
public class ThemeStepDefinitions(TestWorld world)
{
    private IPage Page => world.Page;

    [Given(@"the PWA startup loader is held open with the ""(.*)"" theme")]
    public async Task GivenStartupLoaderIsHeldOpen(string theme)
    {
        await Page.EvaluateAsync("navigator.serviceWorker.getRegistrations().then(items => Promise.all(items.map(item => item.unregister())))");
        await Page.EvaluateAsync("theme => localStorage.setItem('esp-controller.theme', JSON.stringify(theme))", theme);
        await Page.RouteAsync("**/_framework/**", route => route.AbortAsync());
        await Page.GotoAsync($"{AppServer.BaseUrl}/?loader-theme={theme}", new() { WaitUntil = WaitUntilState.DOMContentLoaded });
        await Page.Locator(".esb-loader-card").WaitForAsync();
    }

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

    [Then(@"the startup loader uses the ""(.*)"" product palette")]
    public async Task ThenLoaderUsesProductPalette(string theme)
    {
        await Assertions.Expect(Page.Locator("html")).ToHaveAttributeAsync("data-esb-theme", theme);
        var cardColor = await Page.Locator(".esb-loader-card").EvaluateAsync<string>("element => getComputedStyle(element).backgroundColor");
        var accentColor = await Page.Locator(".esb-loader-dot").EvaluateAsync<string>("element => getComputedStyle(element).backgroundColor");
        Xunit.Assert.Equal(theme == "light" ? "rgb(255, 255, 255)" : "rgb(19, 21, 25)", cardColor);
        Xunit.Assert.Equal(theme == "light" ? "rgb(12, 140, 130)" : "rgb(45, 217, 198)", accentColor);
    }

    [Then("the startup loader presents the barcode controller brand")]
    public async Task ThenLoaderPresentsBrand()
    {
        await Assertions.Expect(Page.Locator(".esb-loader-brand")).ToHaveTextAsync("ESP Barcode Control");
        await Assertions.Expect(Page.Locator(".esb-loader-barcode")).ToBeVisibleAsync();
        await Assertions.Expect(Page.Locator(".loading-progress-text")).ToBeVisibleAsync();
    }
}
