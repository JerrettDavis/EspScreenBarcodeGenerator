using EspBarcode.Controller.Web.Models;

namespace EspBarcode.Controller.Web.Services;

/// <summary>The browser-local "Storage" panel's backing store — saved barcode specs a user can push to
/// any connected device, independent of that device's own on-board LittleFS preset store.</summary>
public sealed class BarcodeLibraryService(LocalStorageService storage)
{
    private const string Key = "esp-controller.library.v1";
    private List<LibraryItem>? _cache;

    public event Action? Changed;

    public async Task<IReadOnlyList<LibraryItem>> GetAllAsync()
    {
        _cache ??= await storage.GetAsync<List<LibraryItem>>(Key) ?? [];
        return _cache;
    }

    public async Task SaveAsync(LibraryItem item)
    {
        await GetAllAsync();
        _cache!.RemoveAll(i => i.Id == item.Id);
        _cache.Insert(0, item);
        await PersistAsync();
    }

    public async Task DeleteAsync(string id)
    {
        await GetAllAsync();
        _cache!.RemoveAll(i => i.Id == id);
        await PersistAsync();
    }

    private async Task PersistAsync()
    {
        await storage.SetAsync(Key, _cache);
        Changed?.Invoke();
    }
}
