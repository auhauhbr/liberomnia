import { onBeforeUnmount, ref } from 'vue'
import { ApiError } from '../services/api'
import { cancelSearchSession, createSearchSession, getSearchResults } from '../services/search'
import type { BibliographicRecord, SearchRequest, SearchSession } from '../types/api'

export function useSearchSession(options = { interval: 750, maxPolls: 60, maxDuration: 60000 }) {
  const session = ref<SearchSession | null>(null)
  const records = ref<BibliographicRecord[]>([])
  const busy = ref(false)
  const error = ref('')
  const cancelling = ref(false)
  let generation = 0
  let controller: AbortController | undefined
  let timer: ReturnType<typeof setTimeout> | undefined
  let polls = 0
  let started = 0
  let disposed = false

  function stopRequests() {
    clearTimeout(timer)
    controller?.abort()
    controller = new AbortController()
  }
  function describe(failure: unknown) {
    if (failure instanceof ApiError && failure.status === 429) return 'Limite de pesquisas atingido. Tente novamente mais tarde.'
    if (failure instanceof ApiError && failure.status === 503) return 'Fonte indisponível para pesquisa.'
    if (failure instanceof Error && !(failure instanceof TypeError)) return failure.message
    return 'Conexão interrompida. Os resultados recebidos foram preservados.'
  }
  async function poll(current: number) {
    if (disposed || current !== generation || !session.value || !controller) return
    if (++polls > options.maxPolls || Date.now() - started >= options.maxDuration) {
      busy.value = false
      error.value = 'Limite de acompanhamento atingido. Cancele a sessão ou inicie outra pesquisa.'
      return
    }
    try {
      const response = await getSearchResults(session.value.id, controller.signal)
      if (disposed || current !== generation) return
      if (response.session.id !== session.value?.id) throw new Error('A API retornou uma sessão diferente da solicitada.')
      session.value = response.session
      // Results are complete snapshots. Replacing them avoids duplicates across polls.
      records.value = response.results.map(item => item.record)
      busy.value = !response.session.finished
      if (busy.value) timer = setTimeout(() => { void poll(current) }, options.interval)
    } catch (failure) {
      if (disposed || current !== generation) return
      error.value = describe(failure)
      busy.value = false
    }
  }
  async function start(request: SearchRequest) {
    const current = ++generation
    stopRequests()
    records.value = []; session.value = null; error.value = ''; busy.value = true; cancelling.value = false
    polls = 0; started = Date.now()
    try {
      const response = await createSearchSession(request, controller!.signal)
      if (disposed || current !== generation) return
      session.value = response.session
      if (cancelling.value) { await cancel(); return }
      await poll(current)
    } catch (failure) {
      if (disposed || current !== generation) return
      error.value = describe(failure); busy.value = false; cancelling.value = false
    }
  }
  async function cancel() {
    cancelling.value = true
    if (!session.value) return // Creation may still be returning the session ID.
    const current = ++generation
    stopRequests()
    error.value = ''
    try {
      const response = await cancelSearchSession(session.value.id, controller!.signal)
      if (disposed || current !== generation) return
      if (response.session.id !== session.value.id) throw new Error('A API retornou uma sessão diferente da solicitada.')
      session.value = response.session
      await poll(current) // Retrieve any results preserved by the cancellation.
    } catch (failure) {
      if (disposed || current !== generation) return
      error.value = describe(failure)
    } finally {
      if (!disposed && current === generation) { busy.value = false; cancelling.value = false }
    }
  }
  function resume() {
    if (!session.value || session.value.finished || polls >= options.maxPolls || Date.now() - started >= options.maxDuration) return
    const current = ++generation
    stopRequests(); error.value = ''; busy.value = true
    void poll(current)
  }
  onBeforeUnmount(() => {
    disposed = true; ++generation; clearTimeout(timer); controller?.abort()
  })
  return { session, records, busy, error, cancelling, start, cancel, resume }
}
