using Microsoft.Playwright;

namespace EspBarcode.Controller.Web.E2ETests.Support;

public static class PageExtensions
{
    /// <summary>
    /// Navigates within the running Blazor WebAssembly app by clicking a sidebar link, instead of
    /// <see cref="IPage.GotoAsync"/> — a real browser navigation would tear down and reboot the whole
    /// WASM app (and, with it, every connected <c>DeviceRegistry</c>/fake-serial-port state this test
    /// suite built up), which is never what an in-app "go to this page" step actually means.
    /// </summary>
    public static async Task GoToSpaAsync(this IPage page, string href, string headingText)
    {
        await page.Locator($"a.esb-nav-link[href='{href}']").First.ClickAsync();
        await page.GetByRole(AriaRole.Heading, new PageGetByRoleOptions { Name = headingText, Exact = true }).WaitForAsync();
    }
}
