import { useEffect, useRef, type ReactNode } from 'react'
import { X, Check, ChevronDown } from 'lucide-react'

export function Modal({ title, subtitle, children, onClose, wide = false }: { title: string; subtitle?: string; children: ReactNode; onClose: () => void; wide?: boolean }) {
  const ref = useRef<HTMLDivElement>(null)
  const closeRef = useRef(onClose); closeRef.current = onClose
  useEffect(() => {
    const previous = document.activeElement as HTMLElement | null
    const element = ref.current
    const firstInput = element?.querySelector<HTMLElement>('input,select,textarea')
    ;(firstInput ?? element?.querySelector<HTMLElement>('button'))?.focus()
    const key = (event: KeyboardEvent) => {
      if (event.key === 'Escape') { event.preventDefault(); closeRef.current(); return }
      if (event.key !== 'Tab') return
      const items = [...element!.querySelectorAll<HTMLElement>('button:not(:disabled),input:not(:disabled),select:not(:disabled),textarea:not(:disabled),[tabindex="0"]')]
      const first = items[0], last = items.at(-1)
      if (event.shiftKey && document.activeElement === first) { event.preventDefault(); last?.focus() }
      else if (!event.shiftKey && document.activeElement === last) { event.preventDefault(); first?.focus() }
    }
    document.addEventListener('keydown', key)
    return () => { document.removeEventListener('keydown', key); previous?.focus() }
  }, [])
  return <div className="modal-backdrop" onMouseDown={event => { if (event.target === event.currentTarget) onClose() }}><div ref={ref} role="dialog" aria-modal="true" aria-labelledby="modal-title" className={'modal ' + (wide ? 'modal-wide' : '')}>
    <div className="modal-heading"><div><h2 id="modal-title">{title}</h2>{subtitle && <p>{subtitle}</p>}</div><button className="icon-button" aria-label="关闭对话框" onClick={onClose}><X size={19}/></button></div>{children}
  </div></div>
}
export function Field({ label, hint, children, className = '' }: { label: string; hint?: string; children: ReactNode; className?: string }) {
  return <label className={'field ' + className}><span>{label}</span>{children}{hint && <small>{hint}</small>}</label>
}
export function Select({ value, onChange, children, label }: { value: string; onChange: (value: string) => void; children: ReactNode; label?: string }) {
  return <span className="select-wrap"><select aria-label={label} value={value} onChange={e => onChange(e.target.value)}>{children}</select><ChevronDown size={14}/></span>
}
export function Switch({ checked, onChange, label, disabled = false }: { checked: boolean; onChange: (value: boolean) => void; label: string; disabled?: boolean }) {
  return <button type="button" role="switch" aria-checked={checked} aria-label={label} disabled={disabled} className={'switch ' + (checked ? 'checked' : '')} onClick={() => onChange(!checked)}><span>{checked && <Check size={10}/>}</span></button>
}
export function Time({ value }: { value: string }) { return <time dateTime={value} title={new Date(value).toLocaleString('zh-CN')}>{new Date(value).toLocaleDateString('zh-CN', { month: '2-digit', day: '2-digit' })}</time> }
