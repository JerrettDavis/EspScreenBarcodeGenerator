using EspBarcode.Controller.Web.E2ETests.Support;
using Microsoft.Playwright;
using Reqnroll;

namespace EspBarcode.Controller.Web.E2ETests.StepDefinitions;

[Binding]
public class GeneratorStepDefinitions(TestWorld world)
{
    private IPage Page => world.Page;

    [When("I set the generator type to {string} and data to {string}")]
    public async Task WhenISetGeneratorTypeAndData(string type, string data)
    {
        await GotoGeneratorAsync();
        await Page.Locator("[data-testid=generator-type]").SelectOptionAsync(type);
        await Page.Locator("[data-testid=generator-data]").FillAsync(data);
    }

    [When("I select the first device as a generator target")]
    public async Task WhenISelectFirstDeviceAsTarget()
        => await Page.Locator("[data-testid=generator-target-checkbox]").First.CheckAsync();

    [When("I select the first Bluetooth generator target")]
    public async Task WhenISelectFirstBluetoothTarget()
        => await Page.Locator("[data-testid=generator-target-checkbox][data-kind=bluetooth]").Nth(0).CheckAsync();

    [When("I select the second Bluetooth generator target")]
    public async Task WhenISelectSecondBluetoothTarget()
        => await Page.Locator("[data-testid=generator-target-checkbox][data-kind=bluetooth]").Nth(1).CheckAsync();

    [When("I select the gateway generator target")]
    public async Task WhenISelectGatewayDirectTarget()
        => await Page.Locator("[data-testid=generator-target-checkbox][data-kind=gatewaydirect]").First.CheckAsync();

    [When("I refresh paired gateway screens on the generator")]
    public async Task WhenIRefreshPairedGatewayScreens()
        => await Page.Locator("[data-testid=refresh-gateway-targets]").ClickAsync();

    [When("I select the first paired gateway generator target")]
    public async Task WhenISelectPairedGatewayTarget()
        => await Page.Locator("[data-testid=generator-target-checkbox][data-kind=gatewaypeer]").First.CheckAsync();

    [When("I click Generate & Push")]
    public async Task WhenIClickGenerateAndPush()
    {
        await Page.Locator("[data-testid=generate-and-push]").ClickAsync();
        await Page.Locator("[data-testid=generator-message]").WaitForAsync();
    }

    [When("I save the generator spec to the library as {string}")]
    public async Task WhenISaveGeneratorSpecToLibrary(string name)
    {
        await Page.Locator("[data-testid=generator-saveas]").FillAsync(name);
        await Page.Locator("[data-testid=save-to-library]").ClickAsync();
        await Page.Locator("[data-testid=generator-message]").WaitForAsync();
    }

    [Then("the generator reports success")]
    public async Task ThenGeneratorReportsSuccess()
        => await Assertions.Expect(Page.Locator("[data-testid=generator-message]")).ToContainTextAsync("Generated");

    [Then("the generator reports it pushed to {int} device\\(s\\)")]
    public async Task ThenGeneratorReportsPushedTo(int count)
        => await Assertions.Expect(Page.Locator("[data-testid=generator-message]"))
            .ToContainTextAsync($"Generated and pushed to {count} device(s).");

    [Then("a live preview is rendered")]
    public async Task ThenLivePreviewIsRendered()
    {
        var canvas = Page.Locator("[data-testid=generator-preview]");
        await canvas.WaitForAsync();
        var width = await canvas.EvaluateAsync<int>("el => el.width");
        Xunit.Assert.True(width > 0, "expected the preview canvas to have been drawn to");
    }

    [Then("the library contains an item named {string}")]
    public async Task ThenLibraryContainsItem(string name)
    {
        await Page.GoToSpaAsync("library", "Storage");
        await Assertions.Expect(Page.Locator("[data-testid=library-table]")).ToContainTextAsync(name);
    }

    private async Task GotoGeneratorAsync() => await Page.GoToSpaAsync("generator", "Generator");
}
