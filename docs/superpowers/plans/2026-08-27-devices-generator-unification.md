# Devices/Generator Unification Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Merge Bluetooth connection management into the Devices page, give Generator one unified "Target Devices" list spanning wired/Bluetooth/gateway-ESP-NOW-peer targets, move photo import into Generator, and retire the standalone Wireless page — with a graceful text-summary preview fallback for target kinds that can't yet download a real bitmap.

**Architecture:** A new `GenerationTargetProvider` service turns `DeviceRegistry` and `BluetoothDeviceRegistry` entries into one common `GenerationTarget` shape (`SendAsync` + optional `DownloadPreviewAsync`) that Generator.razor consumes without caring which transport a target uses. Devices.razor grows a second connection-mode button/card set for Bluetooth. Wireless.razor and its BDD coverage are removed only once every scenario they covered has a green replacement elsewhere.

**Tech Stack:** Blazor WebAssembly (.NET), Reqnroll + Playwright BDD/E2E tests (no unit-test project exists for `EspBarcode.Controller.Web` — this repo tests that project exclusively via the E2E suite; this plan follows that existing convention rather than introducing a new test project).

**Spec:** `docs/superpowers/specs/2026-08-27-devices-generator-unification-design.md`

## Global Constraints

- No firmware or `docs/PROTOCOL_V2.md` changes — the v2 `barcode.download` command is an explicitly separate follow-up sub-project (spec "Non-goals").
- `GenerationTarget.DownloadPreviewAsync` is populated only for wired Client-mode targets; `null` for gateway-direct, gateway-peer, and Bluetooth targets (spec "Architecture").
- `DeviceRegistry` and `BluetoothDeviceRegistry` stay two separate services — no merged device model/registry (spec "Non-goals").
- No new Bluetooth device actions (backlight/reboot/orientation) — Bluetooth cards get connection status + Disconnect only, matching what Wireless.razor already exposed (spec "Non-goals").
- Home.razor's dashboard stats and the topbar connected-device badge are not touched — both stay wired-only (spec "Non-goals").
- Gateway.razor is not touched (spec "Non-goals").
- Dispatch across selected targets in `GenerateAndPushAsync` is sequential, not parallel (spec "Architecture" — this deliberately changes Wireless.razor's Bluetooth-only `Task.WhenAll` behavior; see Task 2's test note).
- Every Wireless.feature scenario must have a passing replacement elsewhere before `Wireless.razor`/`Wireless.feature`/`WirelessStepDefinitions.cs` are deleted (Task 6).

---

## Task 1: Bluetooth as a second connection mode on Devices.razor

**Files:**
- Modify: `dotnet/src/EspBarcode.Controller.Web/Pages/Devices.razor`
- Create: `dotnet/tests/EspBarcode.Controller.Web.E2ETests/StepDefinitions/BluetoothStepDefinitions.cs`
- Modify: `dotnet/tests/EspBarcode.Controller.Web.E2ETests/Features/DeviceConnection.feature`
- Modify: `dotnet/tests/EspBarcode.Controller.Web.E2ETests/StepDefinitions/DeviceStepDefinitions.cs`
- Modify: `dotnet/tests/EspBarcode.Controller.Web.E2ETests/Features/Wireless.feature`
- Modify: `dotnet/tests/EspBarcode.Controller.Web.E2ETests/StepDefinitions/WirelessStepDefinitions.cs`

**Interfaces:**
- Produces: `Devices.razor` gets `@inject BluetoothDeviceRegistry Bluetooth`, a `+ Add Bluetooth Screen` button (`data-testid="connect-bluetooth"`), an unsupported-browser alert (`data-testid="ble-unsupported"`), and one `bluetooth-device-card` per connected `BluetoothDevice` (`data-testid="bluetooth-device-card"`, `data-device-id="@device.Id"`) with a `Disconnect` button (exact text "Disconnect"). Later tasks (2, 5) depend on these testids existing.

- [ ] **Step 1: Write the failing scenarios**

Add to `dotnet/tests/EspBarcode.Controller.Web.E2ETests/Features/DeviceConnection.feature` (after the existing scenarios, same file, same `Background`):

```gherkin
  Scenario: The controller is installable and uses mobile navigation
    When I resize the controller to a phone viewport
    Then the PWA manifest is linked
    And navigation is docked to the bottom of the phone viewport

  Scenario: Connecting Bluetooth screens from the Devices page
    When I connect two Bluetooth screens from Devices
    Then the device list shows 2 connected Bluetooth screens

  Scenario: Disconnecting a Bluetooth screen
    When I connect two Bluetooth screens from Devices
    And I disconnect the first Bluetooth screen
    Then the device list shows 1 connected Bluetooth screen
```

Remove the first scenario ("The controller is installable and uses mobile navigation") from `dotnet/tests/EspBarcode.Controller.Web.E2ETests/Features/Wireless.feature` — it's the scenario that starts `Given the controller app is open with fake Bluetooth screens` / `When I resize the controller to a phone viewport`. Delete just that `Scenario:` block (4 lines: the `Scenario:` line and its `Given`/`When`/`Then`/`And`); leave the rest of `Wireless.feature` untouched for now.

Create `dotnet/tests/EspBarcode.Controller.Web.E2ETests/StepDefinitions/BluetoothStepDefinitions.cs`:

```csharp
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
        await Task.Delay(200);
    }

    [Then("the device list shows {int} connected Bluetooth screen(s)")]
    public async Task ThenBluetoothDeviceListShowsCount(int count)
        => await Assertions.Expect(Page.Locator("[data-testid=bluetooth-device-card]")).ToHaveCountAsync(count);
}
```

Move these three step methods out of `WirelessStepDefinitions.cs` and into `DeviceStepDefinitions.cs` (add them as new methods in the `DeviceStepDefinitions` class, using the same `Page` property already there):

```csharp
    [When("I resize the controller to a phone viewport")]
    public async Task WhenIResizeToPhone() => await Page.SetViewportSizeAsync(390, 844);

    [Then("the PWA manifest is linked")]
    public async Task ThenManifestIsLinked()
    {
        await Assertions.Expect(Page.Locator("link[rel=manifest]")).ToHaveAttributeAsync("href", "manifest.webmanifest");
        var response = await Page.APIRequest.GetAsync(AppServer.BaseUrl + "/manifest.webmanifest");
        Assert.True(response.Ok, "expected the PWA manifest to be served");
        var manifest = await response.TextAsync();
        Assert.Contains("\"sizes\": \"192x192\"", manifest);
        Assert.Contains("\"sizes\": \"512x512\"", manifest);
    }

    [Then("navigation is docked to the bottom of the phone viewport")]
    public async Task ThenNavigationIsBottomDocked()
    {
        var nav = Page.Locator(".esb-sidebar"); var box = await nav.BoundingBoxAsync();
        Assert.NotNull(box); Assert.InRange(box!.Y + box.Height, 842, 846);
    }
```

`DeviceStepDefinitions.cs` already has `using Xunit;` and an `Assert` reference (see its existing `ThenEachDeviceReportsFirmware`), so use bare `Assert.*` there (not `Xunit.Assert.*`) to match that file's existing style.

Delete those same three methods (`WhenIResizeToPhone`, `ThenManifestIsLinked`, `ThenNavigationIsBottomDocked`) from `WirelessStepDefinitions.cs`.

- [ ] **Step 2: Run the tests to verify the new scenarios fail**

```bash
cd dotnet
dotnet test tests/EspBarcode.Controller.Web.E2ETests --filter "FullyQualifiedName~DeviceConnection"
```

Expected: FAIL — `connect-bluetooth`/`bluetooth-device-card` don't exist in Devices.razor yet.

- [ ] **Step 3: Implement Devices.razor**

Add `@inject BluetoothDeviceRegistry Bluetooth` under the existing `@inject DeviceRegistry Registry` line (`dotnet/src/EspBarcode.Controller.Web/Pages/Devices.razor:3`).

Replace the top card's button row (currently lines 20-27):

```razor
        <div class="esb-btn-row">
            <button class="esb-btn esb-btn-sm" type="button" @onclick="ReconnectAuthorizedAsync" disabled="@_busy" data-testid="reconnect-authorized">
                Reconnect Known Devices
            </button>
            <button class="esb-btn esb-btn-primary" type="button" @onclick="ConnectNewAsync" disabled="@_busy" data-testid="connect-new-device">
                + Connect Device
            </button>
        </div>
    </div>
</div>
```

with:

```razor
        <div class="esb-btn-row">
            <button class="esb-btn esb-btn-sm" type="button" @onclick="ReconnectAuthorizedAsync" disabled="@_busy" data-testid="reconnect-authorized">
                Reconnect Known Devices
            </button>
            <button class="esb-btn esb-btn-primary" type="button" @onclick="ConnectNewAsync" disabled="@_busy" data-testid="connect-new-device">
                + Connect Device
            </button>
            <button class="esb-btn esb-btn-primary" type="button" @onclick="ConnectBluetoothAsync" disabled="@(!_bleSupported || _busy)" data-testid="connect-bluetooth">
                + Add Bluetooth Screen
            </button>
        </div>
    </div>
    @if (!_bleSupported)
    {
        <div class="esb-alert esb-alert-error" style="margin-top:10px;" data-testid="ble-unsupported">Web Bluetooth is unavailable in this browser.</div>
    }
</div>
```

Change the empty-state condition (currently line 31, `@if (Registry.Devices.Count == 0)`) to:

```razor
@if (Registry.Devices.Count == 0 && Bluetooth.Devices.Count == 0)
```

and its message (currently line 33) to:

```razor
    <div class="esb-empty" data-testid="devices-empty">No devices connected. Click "Connect Device" for a USB screen, or "Add Bluetooth Screen" for a nearby wireless one.</div>
```

Immediately after the closing `}` of the wired `@foreach (var device in Registry.Devices)` loop but still inside the `<div class="esb-grid" data-testid="device-list">` — i.e. right before that grid `</div>` (currently around line 135) — add:

```razor
        @foreach (var device in Bluetooth.Devices)
        {
            <div class="esb-card" data-testid="bluetooth-device-card" data-device-id="@device.Id">
                <div style="display:flex; justify-content:space-between; align-items:start; gap:8px;">
                    <div style="flex:1; min-width:0;">
                        <div style="font-weight:700; margin-bottom:6px;">@device.Name</div>
                        <div class="esb-faint">Bluetooth · firmware @device.Firmware</div>
                    </div>
                    <span class="esb-badge esb-badge-ok">Bluetooth</span>
                </div>
                <div class="esb-btn-row" style="margin-top:12px;">
                    <button class="esb-btn esb-btn-sm esb-btn-danger" type="button" disabled="@_busy"
                            @onclick="@(() => DisconnectBluetoothAsync(device))">Disconnect</button>
                </div>
            </div>
        }
```

In the `@code` block, replace:

```csharp
    protected override void OnInitialized() => Registry.Changed += OnChanged;
```

with:

```csharp
    private bool _bleSupported;

    protected override async Task OnInitializedAsync()
    {
        Registry.Changed += OnChanged;
        Bluetooth.Changed += OnChanged;
        _bleSupported = await Bluetooth.IsSupportedAsync();
    }
```

Add these two methods next to `ForgetAsync`:

```csharp
    private async Task ConnectBluetoothAsync()
    {
        _error = null;
        _busy = true;
        try { await Bluetooth.ConnectAsync(); }
        catch (Exception ex) { _error = $"Could not connect: {ex.Message}"; }
        finally { _busy = false; }
    }

    private async Task DisconnectBluetoothAsync(BluetoothDevice device)
    {
        _busy = true;
        try { await Bluetooth.DisconnectAsync(device); }
        finally { _busy = false; }
    }
```

And update `Dispose`:

```csharp
    public void Dispose()
    {
        Registry.Changed -= OnChanged;
        Bluetooth.Changed -= OnChanged;
    }
```

- [ ] **Step 4: Run the tests to verify they pass**

```bash
dotnet test tests/EspBarcode.Controller.Web.E2ETests --filter "FullyQualifiedName~DeviceConnection"
```

Expected: PASS (all `DeviceConnectionFeature` scenarios, including the 3 new ones).

Also run the shrinking Wireless suite to confirm it still passes with the 3 steps removed:

```bash
dotnet test tests/EspBarcode.Controller.Web.E2ETests --filter "FullyQualifiedName~Wireless"
```

Expected: PASS (4 remaining scenarios untouched).

- [ ] **Step 5: Commit**

```bash
git add dotnet/src/EspBarcode.Controller.Web/Pages/Devices.razor \
  dotnet/tests/EspBarcode.Controller.Web.E2ETests/Features/DeviceConnection.feature \
  dotnet/tests/EspBarcode.Controller.Web.E2ETests/Features/Wireless.feature \
  dotnet/tests/EspBarcode.Controller.Web.E2ETests/StepDefinitions/BluetoothStepDefinitions.cs \
  dotnet/tests/EspBarcode.Controller.Web.E2ETests/StepDefinitions/DeviceStepDefinitions.cs \
  dotnet/tests/EspBarcode.Controller.Web.E2ETests/StepDefinitions/WirelessStepDefinitions.cs
git commit -m "feat(web): add Bluetooth as a connection mode on the Devices page"
```

---

## Task 2: `GenerationTargetProvider` + unified wired/Bluetooth targets in Generator

**Files:**
- Create: `dotnet/src/EspBarcode.Controller.Web/Services/GenerationTarget.cs`
- Modify: `dotnet/src/EspBarcode.Controller.Web/Program.cs`
- Modify: `dotnet/src/EspBarcode.Controller.Web/Pages/Generator.razor`
- Modify: `dotnet/tests/EspBarcode.Controller.Web.E2ETests/Features/Generator.feature`
- Modify: `dotnet/tests/EspBarcode.Controller.Web.E2ETests/StepDefinitions/GeneratorStepDefinitions.cs`
- Modify: `dotnet/tests/EspBarcode.Controller.Web.E2ETests/Features/Wireless.feature`
- Modify: `dotnet/tests/EspBarcode.Controller.Web.E2ETests/StepDefinitions/WirelessStepDefinitions.cs`

**Interfaces:**
- Consumes: `DeviceRegistry.Devices` (`IReadOnlyList<DeviceConnection>`), `DeviceConnection.{Id, Nickname, IsConnected, Mode, Client, GatewayLink, GatewayControlSessionId}` (Services/DeviceConnection.cs), `BluetoothDeviceRegistry.Devices` (`IReadOnlyList<BluetoothDevice>`), `BluetoothDevice.{Id, Name, Client, ControlSessionId, Firmware}` (Services/BluetoothDeviceRegistry.cs), `EspDeviceClient.{GenerateAsync, DownloadCurrentMatrixAsync}`, `GatewayLinkClient.GenerateAsync(GenerateOptions, uint, TimeSpan?, CancellationToken, ushort)`, `GenerateOptions`/`GenerateResult`/`DownloadedMatrix`/`TrustedPeer` (Models/BarcodeModels.cs).
- Produces: `GenerationTargetKind` enum (`Wired`, `GatewayDirect`, `GatewayPeer`, `Bluetooth`), `GenerationTarget` record (`Id`, `DisplayName`, `Kind`, `SendAsync`, `DownloadPreviewAsync`), `GenerationTargetProvider.GetImmediateTargets(): IReadOnlyList<GenerationTarget>`, `GenerationTargetProvider.GetGatewayDirectTarget(DeviceConnection): GenerationTarget`, `GenerationTargetProvider.BuildPeerTargets(DeviceConnection, IReadOnlyList<TrustedPeer>): IReadOnlyList<GenerationTarget>` — Task 3 depends on the latter two.

- [ ] **Step 1: Write the failing scenario**

Add to `dotnet/tests/EspBarcode.Controller.Web.E2ETests/Features/Generator.feature`:

```gherkin
  Scenario: Sending a barcode to two nearby Bluetooth screens
    Given I connect two Bluetooth screens from Devices
    When I set the generator type to "Qr" and data to "MOBILE-LAB-001"
    And I select the first Bluetooth generator target
    And I select the second Bluetooth generator target
    And I click Generate & Push
    Then the generator reports it pushed to 2 device(s)
```

(This intentionally drops the old `And both Bluetooth screen writes overlapped` assertion — see Global Constraints: dispatch is sequential now, so concurrent-write telemetry is no longer a meaningful check.)

Remove the "Sending a barcode to two nearby Bluetooth screens" scenario from `Wireless.feature`.

Add these steps to `GeneratorStepDefinitions.cs`:

```csharp
    [When("I select the first Bluetooth generator target")]
    public async Task WhenISelectFirstBluetoothTarget()
        => await Page.Locator("[data-testid=generator-target-checkbox][data-kind=bluetooth]").Nth(0).CheckAsync();

    [When("I select the second Bluetooth generator target")]
    public async Task WhenISelectSecondBluetoothTarget()
        => await Page.Locator("[data-testid=generator-target-checkbox][data-kind=bluetooth]").Nth(1).CheckAsync();

    [Then("the generator reports it pushed to {int} device(s)")]
    public async Task ThenGeneratorReportsPushedTo(int count)
        => await Assertions.Expect(Page.Locator("[data-testid=generator-message]"))
            .ToContainTextAsync($"Generated and pushed to {count} device(s).");
```

Remove `WhenIConnectTwoScreens`, `WhenIEnterData`, `WhenISend`, `ThenSentToTwo`, `ThenBluetoothWritesOverlapped` from `WirelessStepDefinitions.cs` (all four were exclusive to this scenario; `WhenIEnterData`/`WhenISend` are also used by the photo-import and gateway scenarios still in `Wireless.feature` at this point — **do not remove those two yet**, only remove `WhenIConnectTwoScreens`, `ThenSentToTwo`, and `ThenBluetoothWritesOverlapped`).

- [ ] **Step 2: Run the test to verify it fails**

```bash
dotnet test tests/EspBarcode.Controller.Web.E2ETests --filter "FullyQualifiedName~Generator"
```

Expected: FAIL — `generator-target-checkbox` elements have no `data-kind` attribute yet, and Generator's target list doesn't include Bluetooth devices.

- [ ] **Step 3: Implement `GenerationTarget.cs`**

Create `dotnet/src/EspBarcode.Controller.Web/Services/GenerationTarget.cs`:

```csharp
using EspBarcode.Controller.Web.Models;

namespace EspBarcode.Controller.Web.Services;

public enum GenerationTargetKind { Wired, GatewayDirect, GatewayPeer, Bluetooth }

/// <summary>
/// One thing a barcode can be sent to, regardless of which transport it's actually reachable
/// over — built by <see cref="GenerationTargetProvider"/> from <see cref="DeviceRegistry"/> and
/// <see cref="BluetoothDeviceRegistry"/> so Generator.razor can offer one flat "Target Devices"
/// list instead of branching per connection kind.
/// </summary>
public sealed record GenerationTarget(
    string Id,
    string DisplayName,
    GenerationTargetKind Kind,
    Func<GenerateOptions, CancellationToken, Task<GenerateResult>> SendAsync,
    Func<CancellationToken, Task<DownloadedMatrix>>? DownloadPreviewAsync);

/// <summary>
/// Builds <see cref="GenerationTarget"/>s from the two device registries. <c>DownloadPreviewAsync</c>
/// is populated only for <see cref="GenerationTargetKind.Wired"/> targets today — only
/// <see cref="EspDeviceClient"/> (v1 protocol) has a download-current-matrix command;
/// <see cref="GatewayLinkClient"/> (v2, used by Bluetooth and gateway-relay targets) does not yet.
/// </summary>
public sealed class GenerationTargetProvider(DeviceRegistry wired, BluetoothDeviceRegistry bluetooth)
{
    /// <summary>Wired Client-mode devices and Bluetooth devices — always available, no round trip.</summary>
    public IReadOnlyList<GenerationTarget> GetImmediateTargets()
    {
        var targets = new List<GenerationTarget>();

        foreach (var device in wired.Devices.Where(d => d.IsConnected && d.Mode == DeviceMode.Client))
        {
            targets.Add(new GenerationTarget(
                $"wired:{device.Id}", device.Nickname, GenerationTargetKind.Wired,
                (options, ct) => device.Client.GenerateAsync(options, ct),
                ct => device.Client.DownloadCurrentMatrixAsync(ct: ct)));
        }

        foreach (var device in bluetooth.Devices)
        {
            targets.Add(new GenerationTarget(
                $"ble:{device.Id}", device.Name, GenerationTargetKind.Bluetooth,
                (options, ct) => device.Client.GenerateAsync(options, device.ControlSessionId, cancellationToken: ct),
                DownloadPreviewAsync: null));
        }

        return targets;
    }

    /// <summary>
    /// A wired Gateway-relay device itself, addressed directly (routeId 0) — the same target
    /// Wireless.razor's "wired:{id}" send path already used for a connected Gateway-relay board;
    /// semantics unchanged, just centralized here.
    /// </summary>
    public GenerationTarget GetGatewayDirectTarget(DeviceConnection gateway)
    {
        var link = gateway.GatewayLink
            ?? throw new InvalidOperationException($"Device '{gateway.Nickname}' is not in gateway mode.");

        return new GenerationTarget(
            $"wired:{gateway.Id}", gateway.Nickname, GenerationTargetKind.GatewayDirect,
            (options, ct) => link.GenerateAsync(options, gateway.GatewayControlSessionId, cancellationToken: ct),
            DownloadPreviewAsync: null);
    }

    /// <summary>
    /// One gateway's trusted ESP-NOW peers, addressed by routeId. The caller decides when to fetch
    /// <paramref name="peers"/> (an explicit <c>GatewayLinkClient.ListTrustedPeersAsync</c> round trip) —
    /// this only turns already-fetched peers into targets, matching Wireless.razor's
    /// "Refresh Paired Gateway Screens" button today.
    /// </summary>
    public IReadOnlyList<GenerationTarget> BuildPeerTargets(DeviceConnection gateway, IReadOnlyList<TrustedPeer> peers)
    {
        var link = gateway.GatewayLink
            ?? throw new InvalidOperationException($"Device '{gateway.Nickname}' is not in gateway mode.");

        return peers.Select(peer => new GenerationTarget(
            $"peer:{gateway.Id}:{peer.RouteId}",
            string.IsNullOrWhiteSpace(peer.Label) ? peer.Fingerprint : peer.Label,
            GenerationTargetKind.GatewayPeer,
            (options, ct) => link.GenerateAsync(
                options, gateway.GatewayControlSessionId, cancellationToken: ct, routeId: checked((ushort)peer.RouteId)),
            DownloadPreviewAsync: null)).ToArray();
    }
}
```

Register it in `dotnet/src/EspBarcode.Controller.Web/Program.cs`, next to the other singletons:

```csharp
builder.Services.AddSingleton<GenerationTargetProvider>();
```

- [ ] **Step 4: Implement Generator.razor's target list and dispatch**

Add `@inject BluetoothDeviceRegistry Bluetooth` and `@inject GenerationTargetProvider TargetProvider` under the existing `@inject` lines at the top of `Generator.razor`.

Replace the "Target Devices" block (currently):

```razor
        <h3>Target Devices</h3>
        @if (ClientDevices.Count == 0)
        {
            <p class="esb-muted">No client-mode devices connected. Go to <a href="devices">Devices</a> first.</p>
        }
        else
        {
            @foreach (var device in ClientDevices)
            {
                <label class="esb-checkbox" style="margin-bottom:6px;">
                    <input type="checkbox" data-testid="generator-target-checkbox" checked="@_targets.Contains(device.Id)"
                           @onchange="@(e => ToggleTarget(device.Id, (bool)e.Value!))" />
                    @device.Nickname
                </label>
            }
        }
```

with:

```razor
        <h3>Target Devices</h3>
        @{ var immediateTargets = TargetProvider.GetImmediateTargets(); }
        @if (immediateTargets.Count == 0)
        {
            <p class="esb-muted">No devices connected. Go to <a href="devices">Devices</a> first.</p>
        }
        else
        {
            @foreach (var target in immediateTargets)
            {
                <label class="esb-checkbox" style="margin-bottom:6px;">
                    <input type="checkbox" data-testid="generator-target-checkbox" data-kind="@target.Kind.ToString().ToLowerInvariant()"
                           checked="@_targets.Contains(target.Id)"
                           @onchange="@(e => ToggleTarget(target.Id, (bool)e.Value!))" />
                    @target.DisplayName <span class="esb-faint">(@KindLabel(target.Kind))</span>
                </label>
            }
        }
```

Replace `ClientDevices` (currently):

```csharp
    private List<DeviceConnection> ClientDevices =>
        Devices.Devices.Where(d => d.IsConnected && d.Mode == DeviceMode.Client).ToList();
```

with:

```csharp
    private static string KindLabel(GenerationTargetKind kind) => kind switch
    {
        GenerationTargetKind.Bluetooth => "Bluetooth",
        GenerationTargetKind.GatewayPeer => "Gateway peer",
        _ => "Wired",
    };
```

Replace `GenerateAndPushAsync` (currently):

```csharp
    private async Task GenerateAndPushAsync()
    {
        _busy = true;
        _message = null;
        _isError = false;
        var options = _options.Clone();
        options.SaveAs = string.IsNullOrWhiteSpace(_saveAsName) ? null : _saveAsName;

        var succeeded = 0;
        var failures = new List<string>();
        DeviceConnection? firstOk = null;

        foreach (var device in Devices.Devices.Where(d => _targets.Contains(d.Id)))
        {
            try
            {
                _lastResult = await device.Client.GenerateAsync(options);
                succeeded++;
                firstOk ??= device;
            }
            catch (Exception ex)
            {
                failures.Add($"{device.Nickname}: {ex.Message}");
            }
        }

        _message = failures.Count == 0
            ? $"Generated and pushed to {succeeded} device(s)."
            : $"Generated on {succeeded} device(s); failed on {string.Join("; ", failures)}";
        _isError = failures.Count > 0 && succeeded == 0;

        if (firstOk is not null && options.Display)
        {
            try
            {
                var matrix = await firstOk.Client.DownloadCurrentMatrixAsync();
                await Renderer.RenderAsync("generator-preview-canvas", matrix);
            }
            catch
            {
                // Preview is best-effort; the push itself already succeeded/failed above.
            }
        }

        _busy = false;
    }
```

with:

```csharp
    private async Task GenerateAndPushAsync()
    {
        _busy = true;
        _message = null;
        _isError = false;
        _resultSummary = null;
        var options = _options.Clone();
        options.SaveAs = string.IsNullOrWhiteSpace(_saveAsName) ? null : _saveAsName;

        var succeeded = 0;
        var failures = new List<string>();
        GenerationTarget? firstOk = null;
        GenerateResult? firstOkResult = null;

        foreach (var target in TargetProvider.GetImmediateTargets().Where(t => _targets.Contains(t.Id)))
        {
            try
            {
                var result = await target.SendAsync(options, CancellationToken.None);
                succeeded++;
                if (firstOk is null) { firstOk = target; firstOkResult = result; }
            }
            catch (Exception ex)
            {
                failures.Add($"{target.DisplayName}: {ex.Message}");
            }
        }

        _message = failures.Count == 0
            ? $"Generated and pushed to {succeeded} device(s)."
            : $"Generated on {succeeded} device(s); failed on {string.Join("; ", failures)}";
        _isError = failures.Count > 0 && succeeded == 0;

        if (firstOk is not null && options.Display)
        {
            if (firstOk.DownloadPreviewAsync is not null)
            {
                try
                {
                    var matrix = await firstOk.DownloadPreviewAsync(CancellationToken.None);
                    await Renderer.RenderAsync("generator-preview-canvas", matrix);
                }
                catch
                {
                    // Preview is best-effort; the push itself already succeeded/failed above.
                }
            }
            else
            {
                _resultSummary = firstOkResult;
            }
        }

        _busy = false;
    }
```

Replace the `_lastResult`-based preview caption. Field declaration, currently:

```csharp
    private GenerateResult? _lastResult;
```

becomes:

```csharp
    private GenerateResult? _resultSummary;
```

Markup, currently:

```razor
        @if (_lastResult is not null)
        {
            <p class="esb-muted" style="margin-top:10px;">
                @_lastResult.Width×@_lastResult.Height modules · normalized "@_lastResult.NormalizedData"
            </p>
        }
```

becomes:

```razor
        @if (_resultSummary is not null)
        {
            <p class="esb-muted" style="margin-top:10px;" data-testid="generator-result-summary">
                @_resultSummary.Width×@_resultSummary.Height modules · normalized "@_resultSummary.NormalizedData"
            </p>
        }
```

Add a `Bluetooth.Changed` subscription so the target list live-updates. `OnInitialized`, currently:

```csharp
    protected override void OnInitialized() => Devices.Changed += OnChanged;
```

becomes:

```csharp
    protected override void OnInitialized()
    {
        Devices.Changed += OnChanged;
        Bluetooth.Changed += OnChanged;
    }
```

and `Dispose`, currently `Devices.Changed -= OnChanged;`, becomes:

```csharp
    public void Dispose()
    {
        Devices.Changed -= OnChanged;
        Bluetooth.Changed -= OnChanged;
    }
```

- [ ] **Step 5: Run the tests to verify they pass**

```bash
dotnet test tests/EspBarcode.Controller.Web.E2ETests --filter "FullyQualifiedName~Generator"
dotnet test tests/EspBarcode.Controller.Web.E2ETests --filter "FullyQualifiedName~Wireless"
```

Expected: both PASS — Generator's existing wired scenario ("Generating and pushing a QR code to a device") must still show `a live preview is rendered` (its `firstOk.DownloadPreviewAsync` branch is exercised since that target is `Kind.Wired`).

- [ ] **Step 6: Commit**

```bash
git add dotnet/src/EspBarcode.Controller.Web/Services/GenerationTarget.cs \
  dotnet/src/EspBarcode.Controller.Web/Program.cs \
  dotnet/src/EspBarcode.Controller.Web/Pages/Generator.razor \
  dotnet/tests/EspBarcode.Controller.Web.E2ETests/Features/Generator.feature \
  dotnet/tests/EspBarcode.Controller.Web.E2ETests/Features/Wireless.feature \
  dotnet/tests/EspBarcode.Controller.Web.E2ETests/StepDefinitions/GeneratorStepDefinitions.cs \
  dotnet/tests/EspBarcode.Controller.Web.E2ETests/StepDefinitions/WirelessStepDefinitions.cs
git commit -m "feat(web): unify Generator's target list across wired and Bluetooth devices"
```

---

## Task 3: Gateway-direct and gateway-peer targets in Generator

**Files:**
- Modify: `dotnet/src/EspBarcode.Controller.Web/Pages/Generator.razor`
- Modify: `dotnet/tests/EspBarcode.Controller.Web.E2ETests/Features/Generator.feature`
- Modify: `dotnet/tests/EspBarcode.Controller.Web.E2ETests/StepDefinitions/GeneratorStepDefinitions.cs`
- Modify: `dotnet/tests/EspBarcode.Controller.Web.E2ETests/StepDefinitions/GatewayStepDefinitions.cs`
- Modify: `dotnet/tests/EspBarcode.Controller.Web.E2ETests/Features/Wireless.feature`
- Modify: `dotnet/tests/EspBarcode.Controller.Web.E2ETests/StepDefinitions/WirelessStepDefinitions.cs`

**Interfaces:**
- Consumes: `GenerationTargetProvider.GetGatewayDirectTarget`/`BuildPeerTargets` (Task 2), `GatewayLinkClient.ListTrustedPeersAsync()` (Services/GatewayLinkClient.cs), `TrustedPeer` (Models/BarcodeModels.cs).

- [ ] **Step 1: Write the failing scenarios**

Add to `Generator.feature`:

```gherkin
  Scenario: Sending through a wired ESP-NOW gateway to itself
    Given I put the first device into gateway mode
    When I set the generator type to "Qr" and data to "GATEWAY-DIRECT-001"
    And I select the gateway generator target
    And I click Generate & Push
    Then the generator reports it pushed to 1 device(s)

  Scenario: Addressing one paired screen through its gateway route
    Given a gateway has a paired screen on route 7
    When I set the generator type to "Qr" and data to "ROUTED-SCREEN-007"
    And I refresh paired gateway screens on the generator
    And I select the first paired gateway generator target
    And I click Generate & Push
    Then the generator reports it pushed to 1 device(s)
```

Remove "Sending through a wired ESP-NOW gateway from the mobile workflow" and "Addressing one paired screen through its gateway route" from `Wireless.feature` (this empties `Wireless.feature` down to just its `Feature:`/description header — leave that header in place, Task 6 deletes the file itself).

Move `GivenGatewayHasRoutedScreen` from `WirelessStepDefinitions.cs` into `GatewayStepDefinitions.cs` unchanged:

```csharp
    [Given("a gateway has a paired screen on route 7")]
    public async Task GivenGatewayHasRoutedScreen(TestWorld world)
    {
        await world.ConfigureFakeDevicesAsync(2, trustedPeers:
            [new { fingerprint = "ROUTE-0007", mac = "AA:BB:CC:DD:EE:07", route_id = 7, paired_at_ms = 10, label = "Lab Screen 7" }],
            requiredRouteId: 7);
        await Page.GoToSpaAsync("devices", "Devices");
        await Page.Locator("[data-testid=reconnect-authorized]").ClickAsync();
        await Assertions.Expect(Page.Locator("[data-testid=device-card]")).ToHaveCountAsync(2);
        var firstCard = Page.Locator("[data-testid=device-card]").First;
        await firstCard.Locator("[data-testid=enter-gateway]").ClickAsync();
        await Assertions.Expect(firstCard.GetByText("Gateway relay")).ToBeVisibleAsync();
        await Page.GoToSpaAsync("gateway", "Gateway");
        await Assertions.Expect(Page.Locator("[data-testid=gateway-card]")).ToHaveCountAsync(1);
    }
```

`GatewayStepDefinitions` is constructor-injected with `TestWorld world` already (`public class GatewayStepDefinitions(TestWorld world)`), so drop the `TestWorld world` parameter from the method signature above and use the class's own `world` field, matching every other method in that file — i.e. the method becomes:

```csharp
    [Given("a gateway has a paired screen on route 7")]
    public async Task GivenGatewayHasRoutedScreen()
    {
        await world.ConfigureFakeDevicesAsync(2, trustedPeers:
            [new { fingerprint = "ROUTE-0007", mac = "AA:BB:CC:DD:EE:07", route_id = 7, paired_at_ms = 10, label = "Lab Screen 7" }],
            requiredRouteId: 7);
        await Page.GoToSpaAsync("devices", "Devices");
        await Page.Locator("[data-testid=reconnect-authorized]").ClickAsync();
        await Assertions.Expect(Page.Locator("[data-testid=device-card]")).ToHaveCountAsync(2);
        var firstCard = Page.Locator("[data-testid=device-card]").First;
        await firstCard.Locator("[data-testid=enter-gateway]").ClickAsync();
        await Assertions.Expect(firstCard.GetByText("Gateway relay")).ToBeVisibleAsync();
        await Page.GoToSpaAsync("gateway", "Gateway");
        await Assertions.Expect(Page.Locator("[data-testid=gateway-card]")).ToHaveCountAsync(1);
    }
```

Remove that method from `WirelessStepDefinitions.cs`.

Add to `GeneratorStepDefinitions.cs`:

```csharp
    [When("I select the gateway generator target")]
    public async Task WhenISelectGatewayDirectTarget()
        => await Page.Locator("[data-testid=generator-target-checkbox][data-kind=gatewaydirect]").First.CheckAsync();

    [When("I refresh paired gateway screens on the generator")]
    public async Task WhenIRefreshPairedGatewayScreens()
        => await Page.Locator("[data-testid=refresh-gateway-targets]").ClickAsync();

    [When("I select the first paired gateway generator target")]
    public async Task WhenISelectPairedGatewayTarget()
        => await Page.Locator("[data-testid=generator-target-checkbox][data-kind=gatewaypeer]").First.CheckAsync();
```

Remove `WhenISelectFirstWiredTarget`, `WhenIRefreshPairedScreens`, `WhenISelectPairedScreen`, `ThenSentToOne` from `WirelessStepDefinitions.cs` (the last user of `GivenOpenWithBluetoothScreens` in `Wireless.feature` is now only the still-there photo-import scenario — leave `GivenOpenWithBluetoothScreens`, `WhenIEnterData`, `WhenISend` in place for Task 4 to remove).

- [ ] **Step 2: Run the tests to verify they fail**

```bash
dotnet test tests/EspBarcode.Controller.Web.E2ETests --filter "FullyQualifiedName~Generator"
```

Expected: FAIL — no `data-kind=gatewaydirect`/`gatewaypeer` checkboxes or `refresh-gateway-targets` button exist in Generator.razor yet.

- [ ] **Step 3: Implement Generator.razor**

Add `@inject DeviceRegistry Devices` is already present; no new injects needed for this task.

Add a `_gatewayPeerTargets` field and an `AllTargets()` helper. In the `@code` block, add:

```csharp
    private readonly Dictionary<string, IReadOnlyList<GenerationTarget>> _gatewayPeerTargets = [];

    private List<GenerationTarget> AllTargets()
    {
        var targets = TargetProvider.GetImmediateTargets().ToList();
        foreach (var gateway in Devices.Devices.Where(d => d.IsConnected && d.Mode == DeviceMode.GatewayRelay))
        {
            targets.Add(TargetProvider.GetGatewayDirectTarget(gateway));
            if (_gatewayPeerTargets.TryGetValue(gateway.Id, out var peers)) targets.AddRange(peers);
        }
        return targets;
    }

    private async Task RefreshGatewayTargetsAsync(DeviceConnection gateway)
    {
        _busy = true;
        try
        {
            var peers = await gateway.GatewayLink!.ListTrustedPeersAsync();
            _gatewayPeerTargets[gateway.Id] = TargetProvider.BuildPeerTargets(gateway, peers);
        }
        catch (Exception ex) { _message = ex.Message; _isError = true; }
        finally { _busy = false; }
    }
```

Replace `GenerateAndPushAsync`'s target-source line (from Task 2):

```csharp
        foreach (var target in TargetProvider.GetImmediateTargets().Where(t => _targets.Contains(t.Id)))
```

with:

```csharp
        foreach (var target in AllTargets().Where(t => _targets.Contains(t.Id)))
```

In the markup, immediately after the "Target Devices" `@if`/`@else` block from Task 2 (right after its closing `}`), add:

```razor
        @foreach (var gateway in Devices.Devices.Where(d => d.IsConnected && d.Mode == DeviceMode.GatewayRelay))
        {
            <div style="margin-top:10px;">
                <label class="esb-checkbox" style="margin-bottom:6px;">
                    <input type="checkbox" data-testid="generator-target-checkbox" data-kind="gatewaydirect"
                           checked="@_targets.Contains($"wired:{gateway.Id}")"
                           @onchange="@(e => ToggleTarget($"wired:{gateway.Id}", (bool)e.Value!))" />
                    @gateway.Nickname <span class="esb-faint">(Wired — gateway)</span>
                </label>
                <button class="esb-btn esb-btn-sm" type="button" data-testid="refresh-gateway-targets"
                        @onclick="@(() => RefreshGatewayTargetsAsync(gateway))">
                    Refresh Paired Gateway Screens
                </button>
                @if (_gatewayPeerTargets.TryGetValue(gateway.Id, out var peerTargets))
                {
                    @foreach (var peer in peerTargets)
                    {
                        <label class="esb-checkbox" style="margin:6px 0 0 20px; padding-left:12px; border-left:2px solid var(--esb-accent);">
                            <input type="checkbox" data-testid="generator-target-checkbox" data-kind="gatewaypeer"
                                   checked="@_targets.Contains(peer.Id)"
                                   @onchange="@(e => ToggleTarget(peer.Id, (bool)e.Value!))" />
                            @peer.DisplayName <span class="esb-faint">(Gateway peer)</span>
                        </label>
                    }
                }
            </div>
        }
```

- [ ] **Step 4: Run the tests to verify they pass**

```bash
dotnet test tests/EspBarcode.Controller.Web.E2ETests --filter "FullyQualifiedName~Generator"
dotnet test tests/EspBarcode.Controller.Web.E2ETests --filter "FullyQualifiedName~Gateway"
dotnet test tests/EspBarcode.Controller.Web.E2ETests --filter "FullyQualifiedName~Wireless"
```

Expected: all PASS.

- [ ] **Step 5: Commit**

```bash
git add dotnet/src/EspBarcode.Controller.Web/Pages/Generator.razor \
  dotnet/tests/EspBarcode.Controller.Web.E2ETests/Features/Generator.feature \
  dotnet/tests/EspBarcode.Controller.Web.E2ETests/Features/Wireless.feature \
  dotnet/tests/EspBarcode.Controller.Web.E2ETests/StepDefinitions/GeneratorStepDefinitions.cs \
  dotnet/tests/EspBarcode.Controller.Web.E2ETests/StepDefinitions/GatewayStepDefinitions.cs \
  dotnet/tests/EspBarcode.Controller.Web.E2ETests/StepDefinitions/WirelessStepDefinitions.cs
git commit -m "feat(web): add gateway-direct and ESP-NOW peer targets to Generator"
```

---

## Task 4: Move photo import into Generator; retire the last of Wireless.feature

**Files:**
- Modify: `dotnet/src/EspBarcode.Controller.Web/Pages/Generator.razor`
- Modify: `dotnet/tests/EspBarcode.Controller.Web.E2ETests/Features/Generator.feature`
- Modify: `dotnet/tests/EspBarcode.Controller.Web.E2ETests/StepDefinitions/GeneratorStepDefinitions.cs`
- Modify: `dotnet/tests/EspBarcode.Controller.Web.E2ETests/Features/Wireless.feature`
- Modify: `dotnet/tests/EspBarcode.Controller.Web.E2ETests/StepDefinitions/WirelessStepDefinitions.cs`

**Interfaces:**
- Consumes: `BarcodeImportService.{IsSupportedAsync, DecodeAsync}` / `ImportedBarcode(Data, Format)` (Services/BarcodeImportService.cs).

- [ ] **Step 1: Write the failing scenario**

Add to `Generator.feature`:

```gherkin
  Scenario: Importing a QR photo prefills a clean request
    When I upload a barcode photo to the generator
    Then the generator barcode type is "Qr"
    And the generator data is "PHOTO-QR-001"
```

Remove "Importing a QR photo prefills a clean request" from `Wireless.feature` — this is the last scenario in the file, so after removing it, `Wireless.feature` has only its `Feature:` header/description left (no `Scenario:` blocks). Leave the header in place; Task 6 deletes the file.

Add to `GeneratorStepDefinitions.cs`:

```csharp
    [When("I upload a barcode photo to the generator")]
    public async Task WhenIUploadPhotoToGenerator()
    {
        await GotoGeneratorAsync();
        await Page.Locator("[data-testid=barcode-photo]").SetInputFilesAsync(TestWorld.SampleImagePath);
        await Page.Locator("[data-testid=decode-photo]").ClickAsync();
    }

    [Then("the generator barcode type is {string}")]
    public async Task ThenGeneratorTypeIs(string type)
        => await Assertions.Expect(Page.Locator("[data-testid=generator-type]")).ToHaveValueAsync(type);

    [Then("the generator data is {string}")]
    public async Task ThenGeneratorDataIs(string data)
        => await Assertions.Expect(Page.Locator("[data-testid=generator-data]")).ToHaveValueAsync(data);
```

Remove `GivenOpenWithBluetoothScreens`, `WhenIOpenUnifiedController`, `WhenIEnterData`, `WhenISend`, `WhenIUploadPhoto`, `ThenTypeIs`, `ThenDataIs` from `WirelessStepDefinitions.cs` — every method in that file should now be gone (`WhenIEnterData`/`WhenISend` were already unused after Task 3 removed the last scenarios that called them; this is where they're deleted). Leave the empty `[Binding] public sealed class WirelessStepDefinitions(TestWorld world) { private IPage Page => world.Page; }` shell (Task 6 deletes the file entirely).

- [ ] **Step 2: Run the test to verify it fails**

```bash
dotnet test tests/EspBarcode.Controller.Web.E2ETests --filter "FullyQualifiedName~Generator"
```

Expected: FAIL — `barcode-photo`/`decode-photo` don't exist on Generator.razor yet.

- [ ] **Step 3: Implement Generator.razor**

Add `@inject BarcodeImportService Importer` under the existing `@inject` lines.

Add, as the first field inside the "Barcode Spec" card (right after `<h2>Barcode Spec</h2>`, before the Type/Rotation `esb-form-row`):

```razor
        <div class="esb-field">
            <label for="gen-photo">Import from photo</label>
            <input id="gen-photo" @ref="_imageInput" type="file" accept="image/*" capture="environment" data-testid="barcode-photo" />
            <button class="esb-btn" type="button" disabled="@(!_importSupported || _busy)" @onclick="DecodeAsync" data-testid="decode-photo">Read Barcode Photo</button>
        </div>
```

In the `@code` block, add fields:

```csharp
    private ElementReference _imageInput;
    private bool _importSupported;
```

Change `OnInitialized` (from Task 2/3) to `OnInitializedAsync`:

```csharp
    protected override async Task OnInitializedAsync()
    {
        Devices.Changed += OnChanged;
        Bluetooth.Changed += OnChanged;
        _importSupported = await Importer.IsSupportedAsync();
    }
```

Add:

```csharp
    private async Task DecodeAsync()
    {
        _busy = true;
        try
        {
            var found = await Importer.DecodeAsync(_imageInput);
            _options.Data = found.Data;
            _options.Type = found.Format switch
            {
                "qr_code" => BarcodeKind.Qr, "code_128" => BarcodeKind.Code128, "code_39" => BarcodeKind.Code39,
                "ean_13" => BarcodeKind.Ean13, "ean_8" => BarcodeKind.Ean8, "upc_a" => BarcodeKind.UpcA,
                _ => _options.Type,
            };
            _message = $"Read {found.Format}; review the data, then send.";
            _isError = false;
        }
        catch (Exception ex) { _message = ex.Message; _isError = true; }
        finally { _busy = false; }
    }
```

- [ ] **Step 4: Run the tests to verify they pass**

```bash
dotnet test tests/EspBarcode.Controller.Web.E2ETests --filter "FullyQualifiedName~Generator"
dotnet test tests/EspBarcode.Controller.Web.E2ETests --filter "FullyQualifiedName~Wireless"
```

Expected: both PASS — `Wireless.feature` now has zero scenarios, so its test run reports 0 tests (not a failure).

- [ ] **Step 5: Commit**

```bash
git add dotnet/src/EspBarcode.Controller.Web/Pages/Generator.razor \
  dotnet/tests/EspBarcode.Controller.Web.E2ETests/Features/Generator.feature \
  dotnet/tests/EspBarcode.Controller.Web.E2ETests/Features/Wireless.feature \
  dotnet/tests/EspBarcode.Controller.Web.E2ETests/StepDefinitions/GeneratorStepDefinitions.cs \
  dotnet/tests/EspBarcode.Controller.Web.E2ETests/StepDefinitions/WirelessStepDefinitions.cs
git commit -m "feat(web): move photo-barcode import into the Generator page"
```

---

## Task 5: Text-summary preview fallback for non-wired targets

**Files:**
- Modify: `dotnet/tests/EspBarcode.Controller.Web.E2ETests/Features/Generator.feature`
- Modify: `dotnet/tests/EspBarcode.Controller.Web.E2ETests/StepDefinitions/GeneratorStepDefinitions.cs`

No production code changes — Task 2 already implemented the `firstOk.DownloadPreviewAsync is null` → `_resultSummary` branch and the `generator-result-summary` markup. This task is the test proving that fallback for a genuinely non-wired (Bluetooth) send, which no earlier task's scenario exercised (Task 2's Bluetooth scenario selected two Bluetooth targets and only ever checked the push-count message, not the preview panel).

**Interfaces:**
- Consumes: `generator-result-summary` (Generator.razor, Task 2), `connect-bluetooth`/`bluetooth-device-card` (Devices.razor, Task 1), `generator-target-checkbox[data-kind=bluetooth]` (Generator.razor, Task 2).

- [ ] **Step 1: Write the failing scenario**

Add to `Generator.feature`:

```gherkin
  Scenario: Result preview falls back to a text summary for a Bluetooth-only target
    Given I connect a Bluetooth screen from Devices
    When I set the generator type to "Qr" and data to "BLE-PREVIEW-001"
    And I select the first Bluetooth generator target
    And I click Generate & Push
    Then the generator reports it pushed to 1 device(s)
    And a text result summary is shown instead of a live preview
```

Add to `GeneratorStepDefinitions.cs`:

```csharp
    [Then("a text result summary is shown instead of a live preview")]
    public async Task ThenTextSummaryShownInsteadOfPreview()
        => await Assertions.Expect(Page.Locator("[data-testid=generator-result-summary]")).ToContainTextAsync("modules");
```

- [ ] **Step 2: Run the test to verify it fails or passes for the wrong reason**

```bash
dotnet test tests/EspBarcode.Controller.Web.E2ETests --filter "FullyQualifiedName~Generator"
```

Expected: this specific scenario should already PASS, since Task 2 implemented the fallback branch and Task 1 implemented Bluetooth connection. If it fails, the bug is in Task 2's `GenerateAndPushAsync`/`_resultSummary` wiring or Task 1's Bluetooth connect flow — fix there, not by adding new production code to this task.

- [ ] **Step 3: Commit**

```bash
git add dotnet/tests/EspBarcode.Controller.Web.E2ETests/Features/Generator.feature \
  dotnet/tests/EspBarcode.Controller.Web.E2ETests/StepDefinitions/GeneratorStepDefinitions.cs
git commit -m "test(web): cover the text-summary preview fallback for Bluetooth targets"
```

---

## Task 6: Remove Wireless.razor and its nav link; final regression pass

**Files:**
- Delete: `dotnet/src/EspBarcode.Controller.Web/Pages/Wireless.razor`
- Delete: `dotnet/tests/EspBarcode.Controller.Web.E2ETests/Features/Wireless.feature`
- Delete: `dotnet/tests/EspBarcode.Controller.Web.E2ETests/StepDefinitions/WirelessStepDefinitions.cs`
- Modify: `dotnet/src/EspBarcode.Controller.Web/Layout/MainLayout.razor`

- [ ] **Step 1: Verify nothing else references the Wireless page**

```bash
grep -rli "wireless" dotnet/src/EspBarcode.Controller.Web --include="*.razor" --include="*.cs"
```

Expected output: exactly `dotnet/src/EspBarcode.Controller.Web/Layout/MainLayout.razor` and `dotnet/src/EspBarcode.Controller.Web/Pages/Wireless.razor` (the two files this task removes/edits). If anything else appears, stop and investigate before deleting.

- [ ] **Step 2: Delete the files**

```bash
git rm dotnet/src/EspBarcode.Controller.Web/Pages/Wireless.razor \
  dotnet/tests/EspBarcode.Controller.Web.E2ETests/Features/Wireless.feature \
  dotnet/tests/EspBarcode.Controller.Web.E2ETests/StepDefinitions/WirelessStepDefinitions.cs
```

- [ ] **Step 3: Remove the nav link**

In `dotnet/src/EspBarcode.Controller.Web/Layout/MainLayout.razor`, delete this line:

```razor
        <NavLink class="esb-nav-link" href="wireless">📱 Wireless</NavLink>
```

- [ ] **Step 4: Full solution build and full E2E regression run**

```bash
cd dotnet
dotnet build EspScreenBarcodeGenerator.slnx
dotnet test tests/EspBarcode.Controller.Web.E2ETests
```

Expected: build succeeds with no warnings about unused `Wireless` references; every scenario across every feature file in the E2E project passes (not just the ones touched by this plan — Library, Theme, Automation, Gateway, Devices, Generator all included, to catch any cross-page regression, e.g. from the removed nav link shifting other layout-dependent assertions).

- [ ] **Step 5: Commit**

```bash
git add dotnet/src/EspBarcode.Controller.Web/Layout/MainLayout.razor
git commit -m "refactor(web): remove the standalone Wireless page

Bluetooth connection now lives on Devices; barcode targeting and
sending — wired, Bluetooth, and gateway ESP-NOW peers — is unified
under Generator. See docs/superpowers/specs/2026-08-27-devices-generator-unification-design.md."
```

---

## Self-Review Notes

- **Spec coverage:** Devices/Bluetooth merge → Task 1. `GenerationTargetProvider` + unified immediate targets → Task 2. Gateway-direct/peer targets → Task 3. Photo import move → Task 4. Preview fallback → Task 2 (implementation) + Task 5 (test). Wireless removal → Tasks 1-4 (incremental) + Task 6 (final deletion). Non-goals (no protocol changes, no registry merge, no new Bluetooth device actions, Home.razor/Gateway.razor untouched) — none of the six tasks touch firmware, `docs/PROTOCOL_V2.md`, merge the two registries, add backlight/reboot/orientation to Bluetooth cards, or modify Home.razor/Gateway.razor.
- **Placeholder scan:** no TBD/TODO; every step has literal code or an exact shell command.
- **Type consistency:** `GenerationTarget`/`GenerationTargetKind`/`GenerationTargetProvider` (Task 2) are used with the same names, method signatures (`GetImmediateTargets()`, `GetGatewayDirectTarget(DeviceConnection)`, `BuildPeerTargets(DeviceConnection, IReadOnlyList<TrustedPeer>)`), and `data-kind` values (`wired`, `gatewaydirect`, `gatewaypeer`, `bluetooth` — lowercased enum names) across Tasks 2, 3, and 5. `_resultSummary`/`GenerateResult` naming is consistent between Task 2's implementation and Task 5's assertion. `bluetooth-device-card` (Task 1) is the same testid Task 5's `Given I connect a Bluetooth screen from Devices` step (Task 1) waits on.
