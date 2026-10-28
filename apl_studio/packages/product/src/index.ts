import type { Context } from 'cordis'
import z from 'schemastery'
import { DatabaseSync } from 'node:sqlite'
import { mkdirSync } from 'node:fs'
import { dirname } from 'node:path'
import { randomUUID } from 'node:crypto'
import { parseDefinition, type Product, type Revision } from '@apl-studio/model'

export const name = 'product'
export interface Config { dbPath: string }
export const Config: z<Config> = z.object({ dbPath: z.string().required() })
export function problem(statusCode: number, message: string, code: string) { return Object.assign(new Error(message), { statusCode, code }) }
type Row = { id: string; revision: number; created_at: string; updated_at: string; definition: string }
export class ProductStore {
  private db: DatabaseSync
  constructor(path: string, private changed: (id: string, revision: number | null) => void = () => {}) {
    mkdirSync(dirname(path), { recursive: true })
    this.db = new DatabaseSync(path)
    this.db.exec(`PRAGMA journal_mode=WAL; PRAGMA foreign_keys=ON;
      CREATE TABLE IF NOT EXISTS products (id TEXT PRIMARY KEY, code TEXT NOT NULL UNIQUE, revision INTEGER NOT NULL, created_at TEXT NOT NULL, updated_at TEXT NOT NULL, definition TEXT NOT NULL);
      CREATE TABLE IF NOT EXISTS revisions (product_id TEXT NOT NULL REFERENCES products(id) ON DELETE CASCADE, revision INTEGER NOT NULL, created_at TEXT NOT NULL, definition TEXT NOT NULL, PRIMARY KEY(product_id, revision));`)
  }
  private decode(row: unknown): Product {
    const r = row as Row
    return { id: r.id, revision: r.revision, createdAt: r.created_at, updatedAt: r.updated_at, definition: JSON.parse(r.definition) }
  }
  list(): Product[] { return this.db.prepare('SELECT * FROM products ORDER BY updated_at DESC, id').all().map(r => this.decode(r)) }
  get(id: string): Product {
    const row = this.db.prepare('SELECT * FROM products WHERE id=?').get(id)
    if (!row) throw problem(404, '产品不存在，可能已被删除', 'NOT_FOUND')
    return this.decode(row)
  }
  create(input: unknown): Product {
    const definition = parseDefinition(input), id = randomUUID(), now = new Date().toISOString()
    this.db.exec('BEGIN IMMEDIATE')
    try {
      this.db.prepare('INSERT INTO products VALUES (?, ?, 1, ?, ?, ?)').run(id, definition.code, now, now, JSON.stringify(definition))
      this.db.prepare('INSERT INTO revisions VALUES (?, 1, ?, ?)').run(id, now, JSON.stringify(definition))
      this.db.exec('COMMIT')
    } catch (e) { this.db.exec('ROLLBACK'); this.rethrow(e) }
    this.changed(id, 1)
    return this.get(id)
  }
  update(id: string, revision: unknown, input: unknown): Product {
    if (!Number.isSafeInteger(revision) || Number(revision) < 1) throw problem(400, '请提供有效的产品修订号', 'INVALID_REVISION')
    const definition = parseDefinition(input), now = new Date().toISOString()
    this.db.exec('BEGIN IMMEDIATE')
    try {
      const current = this.get(id)
      if (current.revision !== revision) throw problem(409, '产品已在其他窗口更新。请保留草稿，读取最新版本后重试。', 'REVISION_CONFLICT')
      this.db.prepare('UPDATE products SET code=?, revision=revision+1, updated_at=?, definition=? WHERE id=?').run(definition.code, now, JSON.stringify(definition), id)
      this.db.prepare('INSERT INTO revisions VALUES (?, ?, ?, ?)').run(id, Number(revision) + 1, now, JSON.stringify(definition))
      this.db.exec('COMMIT')
    } catch (e) { this.db.exec('ROLLBACK'); this.rethrow(e) }
    this.changed(id, Number(revision) + 1)
    return this.get(id)
  }
  remove(id: string, revision: unknown): void {
    const current = this.get(id)
    if (current.revision !== revision) throw problem(409, '产品已更新，请刷新后再删除', 'REVISION_CONFLICT')
    const result = this.db.prepare('DELETE FROM products WHERE id=? AND revision=?').run(id, current.revision)
    if (Number(result.changes) !== 1) throw problem(409, '产品已更新，请刷新后再删除', 'REVISION_CONFLICT')
    this.changed(id, null)
  }
  revisions(id: string): Revision[] {
    this.get(id)
    return this.db.prepare('SELECT revision, created_at AS createdAt FROM revisions WHERE product_id=? ORDER BY revision DESC').all(id) as unknown as Revision[]
  }
  private rethrow(error: unknown): never {
    if (error instanceof Error && error.message.includes('UNIQUE constraint failed: products.code')) throw problem(409, '产品标识已存在，请使用不同的标识', 'DUPLICATE_CODE')
    throw error
  }
  close(): void { this.db.close() }
}
declare module 'cordis' {
  interface Context { products: ProductStore }
  interface Events { 'product/changed'(id: string, revision: number | null): void }
}
export function apply(ctx: Context, config: Config) {
  const store = new ProductStore(config.dbPath, (id, revision) => ctx.emit('product/changed', id, revision))
  ctx.provide('products', store)
  ctx.effect(() => () => store.close())
}
