using EspBarcode.Controller.Web;
using EspBarcode.Controller.Web.Services;
using Microsoft.AspNetCore.Components.Web;
using Microsoft.AspNetCore.Components.WebAssembly.Hosting;

var builder = WebAssemblyHostBuilder.CreateDefault(args);
builder.RootComponents.Add<App>("#app");
builder.RootComponents.Add<HeadOutlet>("head::after");

builder.Services.AddSingleton<WebSerialModule>();
builder.Services.AddSingleton<WebBluetoothModule>();
builder.Services.AddSingleton<BluetoothDeviceRegistry>();
builder.Services.AddSingleton<BarcodeImportService>();
builder.Services.AddSingleton<LocalStorageService>();
builder.Services.AddSingleton<DeviceRegistry>();
builder.Services.AddSingleton<BarcodeLibraryService>();
builder.Services.AddSingleton<ThemeService>();
builder.Services.AddSingleton<AutomationService>();
builder.Services.AddSingleton<BarcodeCanvasRenderer>();

var host = builder.Build();

var theme = host.Services.GetRequiredService<ThemeService>();
await theme.InitializeAsync();

var automation = host.Services.GetRequiredService<AutomationService>();
await automation.InitializeAsync();

await host.RunAsync();
