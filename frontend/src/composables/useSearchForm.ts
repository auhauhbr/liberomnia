import { computed, reactive, ref, watch } from 'vue'

export type SearchMode = 'general' | 'title' | 'author' | 'subject' | 'passage'
type Period = 'any' | 'nineteenth' | 'twentieth' | 'custom'

export function useSearchForm() {
  const form = reactive({
    availability: '', query: '', mode: 'general' as SearchMode, language: '',
    from: '' as number | string, to: '' as number | string, sourceIds: [] as string[],
  })
  const period = ref<Period>('any')
  watch(period, value => {
    if (value === 'nineteenth') { form.from = 1801; form.to = 1900 }
    if (value === 'twentieth') { form.from = 1901; form.to = 2000 }
    if (value === 'any') { form.from = ''; form.to = '' }
  })

  const rangeError = computed(() => {
    const values = [form.from, form.to].filter(value => value !== '').map(Number)
    if (values.some(value => !Number.isInteger(value) || value < 1 || value > 9999)) {
      return 'Informe anos inteiros entre 1 e 9999.'
    }
    if (form.from !== '' && form.to !== '' && Number(form.from) > Number(form.to)) {
      return 'O ano inicial deve ser menor ou igual ao ano final.'
    }
    return ''
  })

  function customizePeriod() { period.value = 'custom' }
  return { form, period, rangeError, customizePeriod }
}
