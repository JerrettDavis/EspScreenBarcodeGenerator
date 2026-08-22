using EspBarcode.Controller.Web.E2ETests.Support;
using Reqnroll;

namespace EspBarcode.Controller.Web.E2ETests.Hooks;

[Binding]
public class Hooks(TestWorld world)
{
    [BeforeTestRun]
    public static async Task BeforeTestRun()
    {
        await AppServer.StartAsync();
        await PlaywrightRunner.StartAsync();
    }

    [AfterTestRun]
    public static async Task AfterTestRun()
    {
        // Each cleanup must run even if the other throws (Playwright's Browser.CloseAsync can throw
        // ObjectDisposedException on some shutdown orderings) — otherwise the dev server process is
        // orphaned and silently serves a stale build to the next test run.
        try { await PlaywrightRunner.StopAsync(); }
        catch (Exception ex) { Console.Error.WriteLine($"PlaywrightRunner.StopAsync failed: {ex}"); }

        try { await AppServer.StopAsync(); }
        catch (Exception ex) { Console.Error.WriteLine($"AppServer.StopAsync failed: {ex}"); }
    }

    [BeforeScenario]
    public async Task BeforeScenario() => await world.InitializeAsync();

    [AfterScenario]
    public async Task AfterScenario() => await world.DisposeAsync();
}
