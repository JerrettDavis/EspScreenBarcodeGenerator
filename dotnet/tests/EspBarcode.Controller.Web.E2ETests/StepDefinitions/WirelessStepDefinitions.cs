using EspBarcode.Controller.Web.E2ETests.Support;
using Microsoft.Playwright;
using Reqnroll;

namespace EspBarcode.Controller.Web.E2ETests.StepDefinitions;

[Binding]
public sealed class WirelessStepDefinitions(TestWorld world)
{
    private IPage Page => world.Page;
}
