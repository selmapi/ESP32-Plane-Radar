// Minimal in-memory stand-in for the Workers `Cache` interface, used only in
// tests so we never depend on a real `caches.default` (which doesn't exist
// under plain Node/vitest).
export class FakeCache {
  private store = new Map<string, Response>();

  async match(request: Request | string): Promise<Response | undefined> {
    const key = typeof request === "string" ? request : request.url;
    const hit = this.store.get(key);
    return hit ? hit.clone() : undefined;
  }

  async put(request: Request | string, response: Response): Promise<void> {
    const key = typeof request === "string" ? request : request.url;
    this.store.set(key, response.clone());
  }

  async delete(request: Request | string): Promise<boolean> {
    const key = typeof request === "string" ? request : request.url;
    return this.store.delete(key);
  }

  get size(): number {
    return this.store.size;
  }
}
