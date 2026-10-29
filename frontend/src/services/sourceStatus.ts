import type { ApiErrorBody, Source } from '../types/api'

export function sourceSearchAllowed(source: Source): boolean {
  return source.integrationType === 'api' && source.implementationStatus === 'available'
    && (source.operational?.searchAllowed ?? true)
}
export function sourceStatusLabel(source: Source): string {
  if (source.operational) {
    const labels: Record<NonNullable<Source['operational']>['state'], string> = {
      disabled: 'Desabilitado', configuration_required: 'Não configurado', unverified: 'Ainda não verificado',
      available: 'Disponível na última consulta', rate_limited: 'Limite temporário', quota_exhausted: 'Cota esgotada',
      configuration_error: 'Revisar configuração', permission_denied: 'Acesso negado',
      communication_error: 'Falha de comunicação', temporary_failure: 'Falha temporária',
    }
    return labels[source.operational.state]
  }
  return source.implementationStatus === 'available' ? 'Disponível'
    : source.implementationStatus === 'temporarily_unavailable' ? 'Indisponível' : 'Planejada'
}
export function diagnosticMessage(error: ApiErrorBody): string {
  const messages: Record<string, string> = {
    provider_configuration_missing: 'Identificação da aplicação ausente. O administrador precisa configurar a integração.',
    provider_configuration_invalid: 'A fonte rejeitou a configuração. É necessária revisão administrativa.',
    provider_key_invalid: 'A fonte rejeitou a chave. É necessária intervenção administrativa.',
    provider_api_disabled: 'A API não está habilitada para esta configuração. Consulte o administrador.',
    provider_key_restricted: 'As restrições da chave impedem esta consulta. Consulte o administrador.',
    provider_authentication: 'A credencial foi rejeitada. É necessária intervenção administrativa.',
    provider_daily_quota_zero: 'A fonte informou cota diária zero. O administrador precisa revisar a cota; aguardar alguns minutos não é uma solução confirmada.',
    provider_daily_quota_exhausted: 'A cota diária está esgotada. O administrador precisa verificar a disponibilidade antes de retomar. O horário de renovação não foi informado.',
    provider_minute_rate_limit: 'Limite por minuto atingido. As consultas estão temporariamente suspensas.',
    provider_rate_limited: 'Limite de consultas atingido. A fonte não confirmou o período da cota. As consultas estão temporariamente suspensas.',
    provider_blocked: 'A fonte negou acesso. É necessária revisão administrativa.',
    provider_network_error: 'Falha de comunicação com a fonte. Uma nova pesquisa poderá tentar novamente após a pausa.',
    provider_timed_out: 'A fonte excedeu o tempo limite de resposta.',
    provider_failed: 'Falha temporária da fonte.',
    invalid_provider_response: 'A fonte retornou uma resposta inválida.',
  }
  return messages[error.code] ?? error.message
}
