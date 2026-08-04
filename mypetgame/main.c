#include "raylib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>


// ==================================================
// CONSTANTES DE BALANCEAMENTO
// ==================================================

#define MAX_COCOS          10
#define NOME_MAX           30

#define NIVEL_EVOLUCAO_1   10      // bebe -> juvenil1 / juvenil2
#define NIVEL_EVOLUCAO_2   30      // juvenil -> adulto

// A evolução é decidida por uma MÉDIA MÓVEL de cada status (não pela média
// desde o nascimento) — reflete como o pet andou sendo cuidado nas últimas
// horas. MEIA_VIDA_MEDIA_EVOLUCAO_HORAS é o tempo pra o peso de uma amostra
// antiga cair pela metade (ver executarTick).
#define MEIA_VIDA_MEDIA_EVOLUCAO_HORAS 6.0f

// Ritmo real de tamagotchi: bebe->juvenil em 24h reais (10 niveis) e
// juvenil->adulto em +48h reais (+20 niveis) => 1 nivel = 24h/10 = 2.4h.
#define INTERVALO_DIA      8640.0f // segundos que equivalem a 1 dia (= 1 nível) do pet

// Um "pulso" de necessidades a cada 20 minutos. Rápido o bastante para o
// jogo reagir, devagar o bastante para não precisar ficar de olho o tempo
// todo — do jeito de um tamagotchi de verdade.
#define INTERVALO_TICK     1200.0f

#define INTERVALO_AUTOSAVE 60.0f   // segundos reais entre salvamentos automáticos (não é ritmo de jogo)

#define MAX_HISTORICO      100     // quantos pets o "pets descobertos" guarda por save

#define PESO_LIMITE_SOBREPESO 80   // acima disso o pet é considerado acima do peso

// Quando o pet recusa refeição/petisco/brincar/remédio, o botão usado fica
// bloqueado por um tempo antes de poder ser tentado de novo.
#define COOLDOWN_RECUSA_SEGUNDOS (3.0f * 60.0f)

// Cocô aparece a cada 4-8h, pedidos de atenção a cada 30min-3h (mais rápido
// com disciplina baixa) — ver calcularProximaAtencao.
#define COCO_INTERVALO_MIN (4.0f * 60.0f * 60.0f)
#define COCO_INTERVALO_MAX (8.0f * 60.0f * 60.0f)

// Período noturno: entre 18h e 6h a energia cai mais rápido acordado, e
// dormir nesse período conta como "sono profundo" (ver alternarDormir).
#define HORA_NOITE_INICIO  18.0f
#define HORA_NOITE_FIM     6.0f

// Agora que o ritmo ao vivo já é realista (em horas), o tempo offline usa
// a MESMA velocidade 1:1 — não precisa mais desacelerar artificialmente.
#define FATOR_TEMPO_OFFLINE        1.0
#define MAX_SEGUNDOS_OFFLINE_REAIS (30.0 * 24.0 * 60.0 * 60.0) // teto de segurança de 30 dias reais


// ==================================================
// ESTÁGIOS DE EVOLUÇÃO DO PET
// ==================================================

typedef enum
{
    ESTAGIO_BEBE = 0,
    ESTAGIO_JUVENIL1,
    ESTAGIO_JUVENIL2,
    ESTAGIO_ADULTO1A,
    ESTAGIO_ADULTO2A,
    ESTAGIO_ADULTO3A,
    ESTAGIO_ADULTO1B,
    ESTAGIO_ADULTO2B,
    ESTAGIO_ADULTO3B,
    ESTAGIO_TOTAL // usado apenas para dimensionar o array de texturas
} EstagioPet;


const char *nomeDoEstagio(EstagioPet estagio)
{
    switch (estagio)
    {
        case ESTAGIO_BEBE:      return "Bebe";
        case ESTAGIO_JUVENIL1:  return "Juvenil 1";
        case ESTAGIO_JUVENIL2:  return "Juvenil 2";
        case ESTAGIO_ADULTO1A:  return "Adulto 1A";
        case ESTAGIO_ADULTO2A:  return "Adulto 2A";
        case ESTAGIO_ADULTO3A:  return "Adulto 3A";
        case ESTAGIO_ADULTO1B:  return "Adulto 1B";
        case ESTAGIO_ADULTO2B:  return "Adulto 2B";
        case ESTAGIO_ADULTO3B:  return "Adulto 3B";
        default:                return "Desconhecido";
    }
}

bool estagioAindaEvolui(EstagioPet estagio)
{
    return (estagio == ESTAGIO_BEBE) ||
           (estagio == ESTAGIO_JUVENIL1) ||
           (estagio == ESTAGIO_JUVENIL2);
}

// Índice em "sonsChoro" (ver main) — adulto1a/1b compartilham som, idem 2a/2b e 3a/3b.
int indiceSomChoro(EstagioPet estagio)
{
    switch (estagio)
    {
        case ESTAGIO_BEBE:     return 0;
        case ESTAGIO_JUVENIL1: return 1;
        case ESTAGIO_JUVENIL2: return 2;
        case ESTAGIO_ADULTO1A:
        case ESTAGIO_ADULTO1B: return 3;
        case ESTAGIO_ADULTO2A:
        case ESTAGIO_ADULTO2B: return 4;
        case ESTAGIO_ADULTO3A:
        case ESTAGIO_ADULTO3B: return 5;
        default:               return 0;
    }
}


// ==================================================
// ESTRUTURA DO PET
// ==================================================

typedef struct
{
    char nome[50];

    int fome;
    int felicidade;
    int energia;
    int saude;
    int disciplina;

    int idade;   // em dias
    int peso;    // em unidades abstratas (gramas)

    bool doente;
    bool dormindo;
    bool vivo;

    bool pedindoAtencao;

    // Motivo do pedido de atenção, fixado no momento em que ele começa (ver
    // gatilho em main() e precisaCuidado): true = fome ou felicidade abaixo
    // de 80 (resolve alimentando/brincando); false = só carência/afeto,
    // ambos já estavam altos (resolve elogiando/repreendendo). Precisa ficar
    // congelado — se recalculássemos a cada clique, um pedido "de carência"
    // podia virar "de necessidade" só por a fome ter caído um pouco enquanto
    // o pedido ainda estava pendente, e elogiar/repreender parava de resolver.
    bool atencaoPorNecessidade;

    int numCocos;
    Vector2 cocos[MAX_COCOS];

    // ---------------- EVOLUÇÃO ----------------

    int nivel;
    EstagioPet estagio;

    // Médias móveis (recentes — não desde o nascimento) de cada atributo,
    // usadas para decidir a evolução (ver executarTick/verificarEvolucao).
    // Ticks recentes pesam muito mais que ticks de várias horas atrás —
    // ver MEIA_VIDA_MEDIA_EVOLUCAO_HORAS.
    double mediaFome;
    double mediaFelicidade;
    double mediaEnergia;
    double mediaSaude;
    double mediaDisciplina;

    // Índice da entrada deste pet em "pets descobertos" (-1 = ainda não
    // apareceu lá). Guardado aqui para que, se o pet chegar a adulto,
    // continuar sendo jogado e depois morrer, a MESMA entrada seja
    // atualizada para RIP em vez de criar uma duplicata.
    int indiceHistorico;

    // Timestamp (Unix, segundos) do último salvamento. Usado para calcular
    // quanto tempo real passou desde então e simular esse tempo offline
    // quando o jogo é reaberto (fome, sono, sujeira, envelhecimento, etc.).
    long long ultimoSalvamento;

    // ---------------- RELÓGIO DE HORA-DO-DIA ----------------
    // A "hora atual" do pet é derivada do relógio real (não é afetada pelo
    // ritmo acelerado do modo dev nem pausada offline): epochCriacao é o
    // timestamp real de quando o pet nasceu, e horaInicialEscolhida é a
    // hora (0-23) que o jogador disse que era "agora" nesse momento. Ver
    // horaAtualDoPet().
    long long epochCriacao;
    float horaInicialEscolhida;

    // true quando o pet foi dormir durante o período noturno (18h-6h): só
    // acorda quando o relógio bater 6h, fome cai mais devagar.
    bool sonoProfundo;

    // Quantas repreensões seguidas (sem um carinho no meio) o pet levou.
    // Decai 1 por tick; zera com elogiar/alimentar/brincar. Usado por
    // repreender() para punir quem fica só repreendendo pra forçar disciplina.
    int repreensoesSeguidas;

} Pet;


// ==================================================
// HISTÓRICO DE PETS ("PETS DESCOBERTOS")
// ==================================================
// Cada pet que morreu ou que o jogador optou por substituir depois de
// chegar a adulto vira uma entrada aqui, persistida no save.

typedef struct
{
    char nome[50];
    EstagioPet estagio; // relevante apenas quando morreu == false (chegou a adulto)
    bool morreu;
} HistoricoPet;

void adicionarAoHistorico(
    HistoricoPet *historico,
    int *numHistorico,
    const char *nome,
    EstagioPet estagio,
    bool morreu
)
{
    if (*numHistorico >= MAX_HISTORICO) return;

    HistoricoPet *entrada = &historico[*numHistorico];

    strncpy(entrada->nome, nome, sizeof(entrada->nome) - 1);
    entrada->nome[sizeof(entrada->nome) - 1] = '\0';
    entrada->estagio = estagio;
    entrada->morreu = morreu;

    (*numHistorico)++;
}


// ==================================================
// ESTADOS DE TELA
// ==================================================

typedef enum
{
    ESTADO_MENU,
    ESTADO_NOMEAR,
    ESTADO_JOGO,
    ESTADO_ADULTO_POPUP,
    ESTADO_HISTORICO,
    ESTADO_MORTE
} EstadoJogo;


// ==================================================
// LIMITAR STATUS
// ==================================================

void limitarStatus(Pet *pet)
{
    if (pet->fome > 100) pet->fome = 100;
    if (pet->fome < 0)   pet->fome = 0;

    if (pet->felicidade > 100) pet->felicidade = 100;
    if (pet->felicidade < 0)   pet->felicidade = 0;

    if (pet->energia > 100) pet->energia = 100;
    if (pet->energia < 0)   pet->energia = 0;

    if (pet->saude > 100) pet->saude = 100;
    if (pet->saude < 0)   pet->saude = 0;

    if (pet->disciplina > 100) pet->disciplina = 100;
    if (pet->disciplina < 0)   pet->disciplina = 0;

    if (pet->peso < 10)  pet->peso = 10;
    if (pet->peso > 200) pet->peso = 200;
}


// ==================================================
// RELÓGIO DE HORA-DO-DIA
// ==================================================
// A hora "atual" do pet é sempre derivada do relógio real do sistema (não
// de um contador que precisa ser atualizado a cada frame), então funciona
// automaticamente mesmo com o app fechado (tempo offline) ou com o tempo
// acelerado do modo dev (que também ajusta epochCriacao, ver main()).

float horaAtualDoPet(Pet *pet)
{
    double segundos = (double)(time(NULL) - pet->epochCriacao);
    double hora = fmod((double)pet->horaInicialEscolhida + segundos / 3600.0, 24.0);

    if (hora < 0.0) hora += 24.0;

    return (float)hora;
}

bool ehPeriodoNoturno(float hora)
{
    return (hora >= HORA_NOITE_INICIO) || (hora < HORA_NOITE_FIM);
}

// Hora local do relógio do sistema (0-23), usada como valor inicial sugerido
// na tela de nomear ("que horas são agora?").
int obterHoraLocalAtual(void)
{
    time_t agora = time(NULL);
    struct tm *horaLocal = localtime(&agora);

    if (horaLocal == NULL) return 12;

    return horaLocal->tm_hour;
}


// ==================================================
// CRIAR NOVO PET (VALORES INICIAIS)
// ==================================================

void iniciarNovoJogo(Pet *pet, const char *nomeEscolhido, int horaEscolhida)
{
    memset(pet, 0, sizeof(Pet));

    if (nomeEscolhido != NULL && nomeEscolhido[0] != '\0')
        strncpy(pet->nome, nomeEscolhido, sizeof(pet->nome) - 1);
    else
        strcpy(pet->nome, "Pet");

    pet->fome = 50;
    pet->felicidade = 50;
    pet->energia = 50;
    pet->saude = 100;
    pet->disciplina = 50;
    pet->idade = 0;
    pet->peso = 40;

    pet->doente = false;
    pet->dormindo = false;
    pet->vivo = true;
    pet->pedindoAtencao = false;
    pet->atencaoPorNecessidade = false;

    pet->numCocos = 0;

    pet->epochCriacao = (long long)time(NULL);
    pet->horaInicialEscolhida = (float)horaEscolhida;
    pet->sonoProfundo = false;
    pet->repreensoesSeguidas = 0;

    pet->nivel = 1;
    pet->estagio = ESTAGIO_BEBE;

    pet->mediaFome = (double)pet->fome;
    pet->mediaFelicidade = (double)pet->felicidade;
    pet->mediaEnergia = (double)pet->energia;
    pet->mediaSaude = (double)pet->saude;
    pet->mediaDisciplina = (double)pet->disciplina;

    pet->indiceHistorico = -1;
    pet->ultimoSalvamento = (long long)time(NULL);
}


// ==================================================
// ATENÇÃO E DISCIPLINA - HELPERS
// ==================================================

// O pet está "precisando de cuidado" (comida/carinho) quando fome ou
// felicidade estão abaixo de 80. Nesse caso, alimentar ou brincar resolve
// o pedido de atenção. Quando ambos estão >= 80, o pedido é só carência/
// afeto, e só elogiar ou repreender resolve.
bool precisaCuidado(Pet *pet)
{
    return (pet->fome < 80) || (pet->felicidade < 80);
}

// Pets com disciplina baixa podem simplesmente recusar cuidados (mesmo
// quando precisam deles). Disciplina 100 = nunca recusa; disciplina 0 =
// recusa quase a metade das vezes.
bool chanceDeRecusa(Pet *pet)
{
    int chance = (100 - pet->disciplina) / 2;

    if (chance <= 0) return false;

    return GetRandomValue(0, 99) < chance;
}

// Disciplina baixa também faz o pet pedir atenção com mais frequência:
// de 30min-1h (disciplina 0) até 1h30-3h (disciplina 100) entre pedidos.
float calcularProximaAtencao(int disciplina)
{
    if (disciplina < 0)   disciplina = 0;
    if (disciplina > 100) disciplina = 100;

    float fator = disciplina / 100.0f;

    int minimo = 1800 + (int)(3600.0f * fator);
    int maximo = 3600 + (int)(7200.0f * fator);

    return (float)GetRandomValue(minimo, maximo);
}


// ==================================================
// ALIMENTAR - REFEICAO (prato principal)
// ==================================================

bool darRefeicao(Pet *pet)
{
    if (pet->dormindo || !pet->vivo) return false;
    if (chanceDeRecusa(pet)) return false;

    bool resolveAtencao = pet->pedindoAtencao && pet->atencaoPorNecessidade;

    pet->fome += 40;
    pet->felicidade += 3;
    pet->peso += 2;
    pet->repreensoesSeguidas = 0; // um carinho perdoa as repreensões recentes

    if (resolveAtencao)
        pet->pedindoAtencao = false;

    limitarStatus(pet);
    return true;
}


// ==================================================
// ALIMENTAR - PETISCO (lanche, mais felicidade, engorda o dobro da refeicao)
// ==================================================

bool darPetisco(Pet *pet)
{
    if (pet->dormindo || !pet->vivo) return false;
    if (chanceDeRecusa(pet)) return false;

    bool resolveAtencao = pet->pedindoAtencao && pet->atencaoPorNecessidade;

    pet->fome += 10;
    pet->felicidade += 15;
    pet->peso += 4; // dobro do ganho de peso da refeicao (+2)
    pet->repreensoesSeguidas = 0;

    if (resolveAtencao)
        pet->pedindoAtencao = false;

    limitarStatus(pet);
    return true;
}


// ==================================================
// BRINCAR
// ==================================================

bool brincar(Pet *pet)
{
    if (pet->dormindo || !pet->vivo) return false;
    if (pet->energia < 20) return false;
    if (chanceDeRecusa(pet)) return false;

    bool resolveAtencao = pet->pedindoAtencao && pet->atencaoPorNecessidade;

    pet->energia -= 20;
    pet->felicidade += 25;
    pet->fome -= 10;
    pet->repreensoesSeguidas = 0;

    if (pet->peso > 10)
        pet->peso -= 1;

    if (resolveAtencao)
        pet->pedindoAtencao = false;

    limitarStatus(pet);
    return true;
}


// ==================================================
// DORMIR (alterna acordado / dormindo)
// ==================================================

void alternarDormir(Pet *pet)
{
    if (!pet->vivo) return;

    if (!pet->dormindo)
    {
        // Indo dormir agora: se for período noturno (18h-6h), é sono profundo.
        pet->dormindo = true;
        pet->sonoProfundo = ehPeriodoNoturno(horaAtualDoPet(pet));
    }
    else if (pet->sonoProfundo)
    {
        // É possível acordar o pet no meio do sono profundo, mas interromper
        // o descanso antes da hora (6h) incomoda e deixa ele mais cansado.
        pet->dormindo = false;
        pet->sonoProfundo = false;
        pet->felicidade -= 15;
        pet->energia -= 10;
        limitarStatus(pet);
    }
    else
    {
        pet->dormindo = false;
    }
}


// ==================================================
// COCO - ADICIONAR / LIMPAR
// ==================================================

void adicionarCoco(Pet *pet)
{
    if (pet->dormindo || !pet->vivo) return;
    if (pet->numCocos >= MAX_COCOS) return;

    float x = (float)GetRandomValue(400, 660);
    float y = (float)GetRandomValue(600, 650);

    pet->cocos[pet->numCocos] = (Vector2){x, y};
    pet->numCocos++;
}

void limparCoco(Pet *pet, int indice)
{
    if (indice < 0 || indice >= pet->numCocos) return;

    for (int i = indice; i < pet->numCocos - 1; i++)
        pet->cocos[i] = pet->cocos[i + 1];

    pet->numCocos--;
}


// ==================================================
// DOENCA - VERIFICAR / CURAR
// ==================================================

void verificarDoenca(Pet *pet)
{
    if (pet->doente) return;

    int risco = 0;

    if (pet->saude < 50)      risco += 15;
    if (pet->numCocos >= 3)   risco += 20;
    if (pet->fome < 15)       risco += 10;

    // Felicidade baixa deixa o pet mais vulnerável a ficar doente, e quanto
    // mais baixa, maior o risco.
    if (pet->felicidade < 50) risco += 10;
    if (pet->felicidade < 20) risco += 15;

    if (risco <= 0) return;

    int sorteio = GetRandomValue(0, 99);

    if (sorteio < risco)
        pet->doente = true;
}

bool darRemedio(Pet *pet)
{
    if (!pet->vivo) return false;
    if (!pet->doente) return false;
    if (chanceDeRecusa(pet)) return false;

    pet->doente = false;
    pet->saude += 30;
    pet->felicidade -= 5; // remedio tem gosto ruim

    limitarStatus(pet);
    return true;
}


// ==================================================
// DISCIPLINA - ELOGIAR / REPREENDER
// ==================================================
// Ambos sempre podem ser usados (não tem chance de recusa), e ambos resolvem
// um pedido de atenção quando esse pedido é só carência/afeto (fome e
// felicidade já estão altas). Quando o pedido é de necessidade (fome ou
// felicidade baixas), só alimentar/brincar resolve — elogiar/repreender não.

void elogiar(Pet *pet)
{
    if (!pet->vivo) return;

    pet->felicidade += 10;
    pet->disciplina -= 5; // elogio em excesso mal-acostuma o pet
    pet->repreensoesSeguidas = 0; // um carinho perdoa as repreensões recentes

    if (pet->pedindoAtencao && !pet->atencaoPorNecessidade)
        pet->pedindoAtencao = false;

    limitarStatus(pet);
}

// Repreender demais sem intervalo (sem nenhum carinho no meio) tem retorno
// decrescente: a partir da 4a repreensão seguida, a disciplina começa a
// CAIR em vez de subir — não dá mais pra chegar a 100 de disciplina só de
// ficar clicando em repreender. repreensoesSeguidas decai com o tempo
// (ver executarTick) e zera com qualquer carinho (elogiar/alimentar/brincar).
void repreender(Pet *pet)
{
    if (!pet->vivo) return;

    pet->repreensoesSeguidas++;

    int penalidadeFelicidade = 10 + (pet->repreensoesSeguidas - 1) * 5;
    int ganhoDisciplina      = 5  - (pet->repreensoesSeguidas - 1) * 2;

    pet->felicidade -= penalidadeFelicidade;
    pet->disciplina += ganhoDisciplina;

    if (pet->pedindoAtencao && !pet->atencaoPorNecessidade)
        pet->pedindoAtencao = false;

    limitarStatus(pet);
}


// ==================================================
// NEGLIGÊNCIA E MORTE
// ==================================================

void aplicarNegligencia(Pet *pet)
{
    if (!pet->vivo) return;

    if (pet->fome <= 0)
        pet->saude -= 3;

    if (pet->numCocos >= MAX_COCOS)
        pet->saude -= 1;

    if (pet->doente)
        pet->saude -= (pet->peso > PESO_LIMITE_SOBREPESO) ? 4 : 2; // acima do peso adoece pior

    if (pet->felicidade <= 0)
        pet->saude -= 1;

    limitarStatus(pet);

    if (pet->saude <= 0)
        pet->vivo = false;
}


// ==================================================
// EVOLUÇÃO
// ==================================================
// Verifica, a cada novo nível, se o pet deve evoluir com base na média
// de cada atributo ao longo de TODO o tempo do estágio atual.
// Se houver evolução, escreve uma mensagem em "mensagem" (buffer do chamador).

void verificarEvolucao(Pet *pet, char *mensagem, size_t tamMensagem, bool *tornouAdulto)
{
    mensagem[0] = '\0';
    *tornouAdulto = false;

    double mediaFome       = pet->mediaFome;
    double mediaFelicidade = pet->mediaFelicidade;
    double mediaEnergia    = pet->mediaEnergia;
    double mediaSaude      = pet->mediaSaude;
    double mediaDisciplina = pet->mediaDisciplina;

    // ---------- BEBE -> JUVENIL1 / JUVENIL2 (nivel 10) ----------

    if ((pet->nivel == NIVEL_EVOLUCAO_1) && (pet->estagio == ESTAGIO_BEBE))
    {
        double mediaGeralBebe = (mediaFome + mediaFelicidade + mediaEnergia + mediaSaude + mediaDisciplina) / 5.0;

        pet->estagio = (mediaGeralBebe >= 60.0) ? ESTAGIO_JUVENIL1 : ESTAGIO_JUVENIL2;

        snprintf(mensagem, tamMensagem, "%s evoluiu para %s!", pet->nome, nomeDoEstagio(pet->estagio));

        return;
    }

    // ---------- JUVENIL1 / JUVENIL2 -> ADULTO (nivel 30) ----------

    if ((pet->nivel == NIVEL_EVOLUCAO_2) &&
        ((pet->estagio == ESTAGIO_JUVENIL1) || (pet->estagio == ESTAGIO_JUVENIL2)))
    {
        double mediaGeral = (mediaFome + mediaFelicidade + mediaEnergia + mediaSaude + mediaDisciplina) / 5.0;

        bool ramoA = (pet->estagio == ESTAGIO_JUVENIL1);

        if (mediaGeral >= 80.0)
            pet->estagio = ramoA ? ESTAGIO_ADULTO1A : ESTAGIO_ADULTO1B;
        else if (mediaGeral >= 50.0)
            pet->estagio = ramoA ? ESTAGIO_ADULTO2A : ESTAGIO_ADULTO2B;
        else
            pet->estagio = ramoA ? ESTAGIO_ADULTO3A : ESTAGIO_ADULTO3B;

        snprintf(mensagem, tamMensagem, "%s evoluiu para %s!", pet->nome, nomeDoEstagio(pet->estagio));
        *tornouAdulto = true;

        return;
    }
}


// ==================================================
// PULSO DE NECESSIDADES / AVANÇO DE DIA
// ==================================================
// Extraídos do loop principal para poderem ser reaproveitados também pela
// simulação de tempo offline (ver simularTempoOffline, abaixo).

// Executa um "pulso" de necessidades (equivalente a INTERVALO_TICK segundos
// de jogo). Retorna true se o pet morreu neste pulso — e nesse caso já
// arquiva (ou atualiza) a entrada dele em "pets descobertos".
bool executarTick(Pet *pet, HistoricoPet *historico, int *numHistorico)
{
    float hora = horaAtualDoPet(pet);

    if (pet->dormindo)
    {
        // Sono profundo (noturno) é bem mais restaurador e a fome fica
        // praticamente parada; sono "normal" (uma soneca de dia) recupera
        // energia também, só que menos, e a fome cai igual a se tivesse
        // acordado — a desaceleração de fome é exclusiva do sono profundo.
        pet->energia += pet->sonoProfundo ? 7 : 5;
        pet->fome -= pet->sonoProfundo ? 0 : 2;
    }
    else
    {
        // Ficar acordado de madrugada cansa mais rápido.
        pet->energia -= ehPeriodoNoturno(hora) ? 2 : 1;
        pet->felicidade -= 1;
        pet->fome -= 2;
    }

    if (pet->pedindoAtencao)
        pet->felicidade -= 1;

    // Repreensões seguidas vão sendo perdoadas com o tempo se o pet não
    // levar mais nenhuma (ver repreender).
    if (pet->repreensoesSeguidas > 0)
        pet->repreensoesSeguidas--;

    // Disciplina relaxa lentamente de volta ao neutro (50) com o tempo, em
    // vez de ficar acumulando sem limite a cada elogio/repreensão.
    if (pet->disciplina > 50)      pet->disciplina -= 1;
    else if (pet->disciplina < 50) pet->disciplina += 1;

    limitarStatus(pet);
    verificarDoenca(pet);
    aplicarNegligencia(pet);

    if (pet->vivo && pet->dormindo)
    {
        if (pet->sonoProfundo)
        {
            // Só acorda quando o relógio bater 6h (sai do período noturno).
            if (!ehPeriodoNoturno(horaAtualDoPet(pet)))
            {
                pet->dormindo = false;
                pet->sonoProfundo = false;
            }
        }
        else if (pet->energia >= 100)
        {
            pet->dormindo = false;
        }
    }

    if (pet->vivo && estagioAindaEvolui(pet->estagio))
    {
        // Média móvel exponencial: cada tick pesa "alfa" e o histórico
        // anterior pesa "1-alfa" — reflete as últimas horas, não a vida
        // toda do pet (ver MEIA_VIDA_MEDIA_EVOLUCAO_HORAS).
        float alfa = 1.0f - powf(0.5f, (INTERVALO_TICK / 3600.0f) / MEIA_VIDA_MEDIA_EVOLUCAO_HORAS);

        pet->mediaFome       += ((double)pet->fome       - pet->mediaFome)       * alfa;
        pet->mediaFelicidade += ((double)pet->felicidade - pet->mediaFelicidade) * alfa;
        pet->mediaEnergia    += ((double)pet->energia    - pet->mediaEnergia)    * alfa;
        pet->mediaSaude      += ((double)pet->saude       - pet->mediaSaude)      * alfa;
        pet->mediaDisciplina += ((double)pet->disciplina - pet->mediaDisciplina) * alfa;
    }

    if (!pet->vivo)
    {
        if (pet->indiceHistorico >= 0)
            historico[pet->indiceHistorico].morreu = true;
        else
            adicionarAoHistorico(historico, numHistorico, pet->nome, pet->estagio, true);

        return true;
    }

    return false;
}

// Executa o avanço de um dia (envelhecimento, nível, evolução). Retorna true
// se o pet chegou a um estágio adulto nesse avanço. "mensagem" recebe o texto
// de evolução (usado pelo banner ao vivo; pode ser descartado no offline).
bool executarAvancoDeDia(Pet *pet, HistoricoPet *historico, int *numHistorico, char *mensagem, size_t tamMensagem)
{
    pet->idade += 1;
    pet->nivel += 1;

    if (pet->peso > 60)
        pet->peso -= 1;

    limitarStatus(pet);

    bool tornouAdulto = false;
    verificarEvolucao(pet, mensagem, tamMensagem, &tornouAdulto);

    if (tornouAdulto && pet->indiceHistorico < 0)
    {
        adicionarAoHistorico(historico, numHistorico, pet->nome, pet->estagio, false);
        pet->indiceHistorico = *numHistorico - 1;
    }

    return tornouAdulto;
}


// ==================================================
// TEMPO OFFLINE (ENQUANTO O JOGO ESTAVA FECHADO)
// ==================================================
// Usa os mesmos "pulsos" de necessidade/dia do jogo ao vivo, mas em ritmo
// bem mais lento (FATOR_TEMPO_OFFLINE) — ver comentário da constante.

void simularTempoOffline(
    Pet *pet,
    HistoricoPet *historico,
    int *numHistorico,
    double segundosOfflineReais,
    bool *tornouAdultoOffline
)
{
    *tornouAdultoOffline = false;

    if (!pet->vivo || segundosOfflineReais <= 0.0) return;

    if (segundosOfflineReais > MAX_SEGUNDOS_OFFLINE_REAIS)
        segundosOfflineReais = MAX_SEGUNDOS_OFFLINE_REAIS;

    double segundosSimulados = segundosOfflineReais * FATOR_TEMPO_OFFLINE;

    double relogioTickOffline = 0.0;
    double relogioIdadeOffline = 0.0;

    char mensagemDescartavel[100];

    while (segundosSimulados > 0.0)
    {
        double passo = (segundosSimulados < 60.0) ? segundosSimulados : 60.0;

        relogioTickOffline += passo;
        relogioIdadeOffline += passo;
        segundosSimulados -= passo;

        while (relogioTickOffline >= INTERVALO_TICK)
        {
            relogioTickOffline -= INTERVALO_TICK;

            if (executarTick(pet, historico, numHistorico))
                return; // pet morreu, a simulação para aqui
        }

        while (relogioIdadeOffline >= INTERVALO_DIA)
        {
            relogioIdadeOffline -= INTERVALO_DIA;

            if (executarAvancoDeDia(pet, historico, numHistorico, mensagemDescartavel, sizeof(mensagemDescartavel)))
                *tornouAdultoOffline = true;

            if (!pet->vivo) return;
        }
    }
}


// ==================================================
// SALVAR / CARREGAR JOGO
// ==================================================

bool caminhoDoSave(char *destino, size_t tamanho)
{
    const char *dir = GetApplicationDirectory();

    int escrito = snprintf(destino, tamanho, "%ssave.dat", dir);

    return (escrito > 0 && escrito < (int)tamanho);
}

bool arquivoDeSaveExiste(void)
{
    char caminho[512];

    if (!caminhoDoSave(caminho, sizeof(caminho)))
        return false;

    FILE *arquivo = fopen(caminho, "rb");

    if (arquivo == NULL)
        return false;

    fclose(arquivo);

    return true;
}

// Formato texto (chave=valor), de propósito: dá para abrir o save.dat num
// editor de texto e alterar status do pet manualmente para testar o jogo.

bool salvarJogo(Pet *pet, HistoricoPet *historico, int numHistorico)
{
    char caminho[512];

    if (!caminhoDoSave(caminho, sizeof(caminho)))
        return false;

    FILE *arquivo = fopen(caminho, "w");

    if (arquivo == NULL)
        return false;

    // Marca "agora" como o último instante em que a simulação esteve em dia,
    // para o próximo carregamento saber quanto tempo real passou desde então.
    pet->ultimoSalvamento = (long long)time(NULL);

    fprintf(arquivo, "[PET]\n");
    fprintf(arquivo, "nome=%s\n", pet->nome);
    fprintf(arquivo, "fome=%d\n", pet->fome);
    fprintf(arquivo, "felicidade=%d\n", pet->felicidade);
    fprintf(arquivo, "energia=%d\n", pet->energia);
    fprintf(arquivo, "saude=%d\n", pet->saude);
    fprintf(arquivo, "disciplina=%d\n", pet->disciplina);
    fprintf(arquivo, "idade=%d\n", pet->idade);
    fprintf(arquivo, "peso=%d\n", pet->peso);
    fprintf(arquivo, "doente=%d\n", pet->doente ? 1 : 0);
    fprintf(arquivo, "dormindo=%d\n", pet->dormindo ? 1 : 0);
    fprintf(arquivo, "vivo=%d\n", pet->vivo ? 1 : 0);
    fprintf(arquivo, "pedindoAtencao=%d\n", pet->pedindoAtencao ? 1 : 0);
    fprintf(arquivo, "atencaoPorNecessidade=%d\n", pet->atencaoPorNecessidade ? 1 : 0);
    fprintf(arquivo, "numCocos=%d\n", pet->numCocos);

    for (int i = 0; i < pet->numCocos; i++)
    {
        fprintf(arquivo, "coco%dx=%.2f\n", i, pet->cocos[i].x);
        fprintf(arquivo, "coco%dy=%.2f\n", i, pet->cocos[i].y);
    }

    fprintf(arquivo, "nivel=%d\n", pet->nivel);
    fprintf(arquivo, "estagio=%d\n", (int)pet->estagio);
    fprintf(arquivo, "mediaFome=%f\n", pet->mediaFome);
    fprintf(arquivo, "mediaFelicidade=%f\n", pet->mediaFelicidade);
    fprintf(arquivo, "mediaEnergia=%f\n", pet->mediaEnergia);
    fprintf(arquivo, "mediaSaude=%f\n", pet->mediaSaude);
    fprintf(arquivo, "mediaDisciplina=%f\n", pet->mediaDisciplina);
    fprintf(arquivo, "indiceHistorico=%d\n", pet->indiceHistorico);
    fprintf(arquivo, "ultimoSalvamento=%lld\n", pet->ultimoSalvamento);
    fprintf(arquivo, "epochCriacao=%lld\n", pet->epochCriacao);
    fprintf(arquivo, "horaInicialEscolhida=%f\n", pet->horaInicialEscolhida);
    fprintf(arquivo, "sonoProfundo=%d\n", pet->sonoProfundo ? 1 : 0);
    fprintf(arquivo, "repreensoesSeguidas=%d\n", pet->repreensoesSeguidas);

    fprintf(arquivo, "\n[HISTORICO]\n");
    fprintf(arquivo, "count=%d\n", numHistorico);

    for (int i = 0; i < numHistorico; i++)
    {
        fprintf(arquivo, "hist%d.nome=%s\n", i, historico[i].nome);
        fprintf(arquivo, "hist%d.estagio=%d\n", i, (int)historico[i].estagio);
        fprintf(arquivo, "hist%d.morreu=%d\n", i, historico[i].morreu ? 1 : 0);
    }

    fclose(arquivo);

    return true;
}

bool carregarJogo(Pet *pet, HistoricoPet *historico, int *numHistorico)
{
    char caminho[512];

    if (!caminhoDoSave(caminho, sizeof(caminho)))
        return false;

    FILE *arquivo = fopen(caminho, "r");

    if (arquivo == NULL)
        return false;

    memset(pet, 0, sizeof(Pet));
    pet->indiceHistorico = -1; // saves antigos (sem essa chave) não devem apontar para o índice 0
    pet->ultimoSalvamento = (long long)time(NULL); // idem: sem essa chave, assume "agora" (sem penalidade offline)
    pet->epochCriacao = (long long)time(NULL);     // idem: saves antigos assumem "hora atual" como referência
    pet->horaInicialEscolhida = (float)obterHoraLocalAtual();
    pet->mediaFome = pet->mediaFelicidade = pet->mediaEnergia = pet->mediaDisciplina = 50.0; // idem
    pet->mediaSaude = 100.0;
    *numHistorico = 0;

    char linha[256];

    while (fgets(linha, sizeof(linha), arquivo) != NULL)
    {
        linha[strcspn(linha, "\r\n")] = '\0';

        char chave[128];
        char valor[128];

        if (sscanf(linha, "%127[^=]=%127[^\n]", chave, valor) != 2)
            continue;

        int idx;
        char sufixo[32];

        if (strcmp(chave, "nome") == 0)
        {
            strncpy(pet->nome, valor, sizeof(pet->nome) - 1);
            pet->nome[sizeof(pet->nome) - 1] = '\0';
        }
        else if (strcmp(chave, "fome") == 0)
            pet->fome = atoi(valor);
        else if (strcmp(chave, "felicidade") == 0)
            pet->felicidade = atoi(valor);
        else if (strcmp(chave, "energia") == 0)
            pet->energia = atoi(valor);
        else if (strcmp(chave, "saude") == 0)
            pet->saude = atoi(valor);
        else if (strcmp(chave, "disciplina") == 0)
            pet->disciplina = atoi(valor);
        else if (strcmp(chave, "idade") == 0)
            pet->idade = atoi(valor);
        else if (strcmp(chave, "peso") == 0)
            pet->peso = atoi(valor);
        else if (strcmp(chave, "doente") == 0)
            pet->doente = (atoi(valor) != 0);
        else if (strcmp(chave, "dormindo") == 0)
            pet->dormindo = (atoi(valor) != 0);
        else if (strcmp(chave, "vivo") == 0)
            pet->vivo = (atoi(valor) != 0);
        else if (strcmp(chave, "pedindoAtencao") == 0)
            pet->pedindoAtencao = (atoi(valor) != 0);
        else if (strcmp(chave, "atencaoPorNecessidade") == 0)
            pet->atencaoPorNecessidade = (atoi(valor) != 0);
        else if (strcmp(chave, "numCocos") == 0)
            pet->numCocos = atoi(valor);
        else if (strcmp(chave, "nivel") == 0)
            pet->nivel = atoi(valor);
        else if (strcmp(chave, "estagio") == 0)
            pet->estagio = (EstagioPet)atoi(valor);
        else if (strcmp(chave, "mediaFome") == 0)
            pet->mediaFome = atof(valor);
        else if (strcmp(chave, "mediaFelicidade") == 0)
            pet->mediaFelicidade = atof(valor);
        else if (strcmp(chave, "mediaEnergia") == 0)
            pet->mediaEnergia = atof(valor);
        else if (strcmp(chave, "mediaSaude") == 0)
            pet->mediaSaude = atof(valor);
        else if (strcmp(chave, "mediaDisciplina") == 0)
            pet->mediaDisciplina = atof(valor);
        else if (strcmp(chave, "indiceHistorico") == 0)
            pet->indiceHistorico = atoi(valor);
        else if (strcmp(chave, "ultimoSalvamento") == 0)
            pet->ultimoSalvamento = atoll(valor);
        else if (strcmp(chave, "epochCriacao") == 0)
            pet->epochCriacao = atoll(valor);
        else if (strcmp(chave, "horaInicialEscolhida") == 0)
            pet->horaInicialEscolhida = (float)atof(valor);
        else if (strcmp(chave, "sonoProfundo") == 0)
            pet->sonoProfundo = (atoi(valor) != 0);
        else if (strcmp(chave, "repreensoesSeguidas") == 0)
            pet->repreensoesSeguidas = atoi(valor);
        else if ((sscanf(chave, "coco%d%1s", &idx, sufixo) == 2) && (idx >= 0) && (idx < MAX_COCOS))
        {
            if (sufixo[0] == 'x')      pet->cocos[idx].x = (float)atof(valor);
            else if (sufixo[0] == 'y') pet->cocos[idx].y = (float)atof(valor);
        }
        else if ((sscanf(chave, "hist%d.%31s", &idx, sufixo) == 2) && (idx >= 0) && (idx < MAX_HISTORICO))
        {
            if (idx >= *numHistorico)
                *numHistorico = idx + 1;

            if (strcmp(sufixo, "nome") == 0)
            {
                strncpy(historico[idx].nome, valor, sizeof(historico[idx].nome) - 1);
                historico[idx].nome[sizeof(historico[idx].nome) - 1] = '\0';
            }
            else if (strcmp(sufixo, "estagio") == 0)
                historico[idx].estagio = (EstagioPet)atoi(valor);
            else if (strcmp(sufixo, "morreu") == 0)
                historico[idx].morreu = (atoi(valor) != 0);
        }
    }

    fclose(arquivo);

    return true;
}


// ==================================================
// DESENHAR BARRA
// ==================================================

void desenharBarra(
    int x,
    int y,
    int largura,
    int altura,
    int valor,
    Color cor
)
{
    DrawRectangle(x, y, largura, altura, LIGHTGRAY);

    DrawRectangle(x, y, largura * valor / 100, altura, cor);

    DrawRectangleLines(x, y, largura, altura, DARKGRAY);
}


// ==================================================
// DESENHAR STATUS
// ==================================================

void desenharStatus(Pet *pet)
{
    int x = 50;
    int y = 120;

    int larguraBarra = 250;
    int alturaBarra = 25;


    DrawText(TextFormat("Fome: %d/100", pet->fome), x, y, 20, DARKGRAY);
    desenharBarra(x, y + 25, larguraBarra, alturaBarra, pet->fome, ORANGE);

    DrawText(TextFormat("Felicidade: %d/100", pet->felicidade), x, y + 70, 20, DARKGRAY);
    desenharBarra(x, y + 95, larguraBarra, alturaBarra, pet->felicidade, YELLOW);

    DrawText(TextFormat("Energia: %d/100", pet->energia), x, y + 140, 20, DARKGRAY);
    desenharBarra(x, y + 165, larguraBarra, alturaBarra, pet->energia, SKYBLUE);

    DrawText(TextFormat("Saude: %d/100", pet->saude), x, y + 210, 20, DARKGRAY);
    desenharBarra(x, y + 235, larguraBarra, alturaBarra, pet->saude, RED);

    DrawText(TextFormat("Disciplina: %d/100", pet->disciplina), x, y + 280, 20, DARKGRAY);
    desenharBarra(x, y + 305, larguraBarra, alturaBarra, pet->disciplina, PURPLE);

    DrawText(TextFormat("Idade: %d dia(s)", pet->idade), x, y + 355, 20, DARKGRAY);

    const char *textoPeso = TextFormat("Peso: %d g", pet->peso);
    DrawText(textoPeso, x, y + 380, 20, DARKGRAY);

    if (pet->peso > PESO_LIMITE_SOBREPESO)
    {
        int largTextoPeso = MeasureText(textoPeso, 20);
        DrawText("Sobrepeso", x + largTextoPeso + 15, y + 380, 20, MAROON);
    }

    if (pet->dormindo)
        DrawText("Estado: dormindo", x, y + 410, 20, DARKBLUE);
    else
        DrawText("Estado: acordado", x, y + 410, 20, DARKGREEN);

    DrawText(TextFormat("Nivel: %d", pet->nivel), x, y + 440, 20, DARKGRAY);
    DrawText(TextFormat("Estagio: %s", nomeDoEstagio(pet->estagio)), x, y + 465, 20, DARKGRAY);
}


// ==================================================
// DESENHAR COCOS
// ==================================================

void desenharCocos(Pet *pet)
{
    for (int i = 0; i < pet->numCocos; i++)
    {
        Vector2 p = pet->cocos[i];

        DrawEllipse((int)p.x, (int)p.y + 6, 10, 6, BROWN);
        DrawEllipse((int)p.x - 4, (int)p.y - 2, 7, 5, BROWN);
        DrawEllipse((int)p.x + 3, (int)p.y - 6, 5, 4, BROWN);
    }
}


// ==================================================
// DESENHAR ICONE DE DOENCA (CAVEIRA)
// ==================================================

void desenharDoenca(Pet *pet, int centroX, int topoY)
{
    if (!pet->doente) return;

    int cx = centroX;
    int cy = topoY;

    DrawCircle(cx, cy, 16, WHITE);
    DrawCircleLines(cx, cy, 16, BLACK);

    DrawCircle(cx - 6, cy - 2, 4, BLACK);
    DrawCircle(cx + 6, cy - 2, 4, BLACK);

    DrawRectangle(cx - 4, cy + 6, 8, 5, BLACK);

    DrawText("DOENTE!", cx - 34, cy + 22, 18, MAROON);
}


// ==================================================
// DESENHAR ICONE DE "PEDINDO ATENCAO"
// ==================================================

void desenharAtencao(Pet *pet, int centroX, int topoY)
{
    if (!pet->pedindoAtencao) return;

    DrawCircle(centroX, topoY, 15, GOLD);
    DrawCircleLines(centroX, topoY, 15, ORANGE);

    DrawText("!", centroX - 4, topoY - 12, 24, MAROON);

    DrawText("O pet quer atencao!", centroX - 90, topoY + 22, 18, DARKBROWN);
}


// ==================================================
// DESENHAR PET
// ==================================================

void desenharPet(
    Pet *pet,
    Texture2D *texturasEstagio,
    Texture2D bebeIdle1,
    Texture2D bebeIdle2,
    Texture2D juvenil1Idle1,
    Texture2D juvenil1Idle2,
    Texture2D juvenil2Idle1,
    Texture2D juvenil2Idle2,
    int framePet,
    int centroX,
    int centroY
)
{
    Texture2D texturaAtual;

    if (pet->estagio == ESTAGIO_BEBE)
        texturaAtual = (framePet == 0) ? bebeIdle1 : bebeIdle2;
    else if (pet->estagio == ESTAGIO_JUVENIL1)
        texturaAtual = (framePet == 0) ? juvenil1Idle1 : juvenil1Idle2;
    else if (pet->estagio == ESTAGIO_JUVENIL2)
        texturaAtual = (framePet == 0) ? juvenil2Idle1 : juvenil2Idle2;
    else
        texturaAtual = texturasEstagio[pet->estagio]; // estagios adultos: sprite unico e estatico

    float escala = 0.45f;

    float largura = texturaAtual.width * escala;
    float altura = texturaAtual.height * escala;

    float x = centroX - largura / 2;
    float y = centroY - altura / 2;

    DrawTextureEx(texturaAtual, (Vector2){x, y}, 0.0f, escala, WHITE);
}


// ==================================================
// DESENHAR HISTÓRICO ("PETS DESCOBERTOS")
// ==================================================

void desenharHistorico(
    HistoricoPet *historico,
    int numHistorico,
    int pagina,
    Texture2D *texturasEstagio,
    int largura
)
{
    const char *titulo = "PETS DESCOBERTOS";
    int tamTitulo = 34;
    int largTitulo = MeasureText(titulo, tamTitulo);

    DrawText(titulo, largura / 2 - largTitulo / 2, 60, tamTitulo, DARKGRAY);

    if (numHistorico <= 0)
    {
        const char *vazio = "Nenhum pet descoberto ainda.";
        int largVazio = MeasureText(vazio, 20);
        DrawText(vazio, largura / 2 - largVazio / 2, 300, 20, GRAY);
        return;
    }

    const int itensPorPagina = 6;

    int inicio = pagina * itensPorPagina;
    int fim = inicio + itensPorPagina;
    if (fim > numHistorico) fim = numHistorico;

    int y = 130;

    for (int i = inicio; i < fim; i++)
    {
        HistoricoPet *h = &historico[i];
        int x = largura / 2 - 250;

        DrawRectangle(x, y, 500, 80, LIGHTGRAY);
        DrawRectangleLines(x, y, 500, 80, DARKGRAY);

        if (h->morreu)
        {
            DrawText(h->nome, x + 20, y + 28, 22, MAROON);
            DrawText("RIP", x + 420, y + 28, 22, MAROON);
        }
        else
        {
            Texture2D tex = texturasEstagio[h->estagio];

            if (tex.id != 0)
            {
                float escala = 70.0f / (float)tex.height;
                DrawTextureEx(tex, (Vector2){ (float)x + 10, (float)y + 5 }, 0.0f, escala, WHITE);
            }

            DrawText(h->nome, x + 100, y + 18, 22, DARKGREEN);
            DrawText("chegou a fase adulta!", x + 100, y + 46, 16, DARKGRAY);
        }

        y += 95;
    }
}


// ==================================================
// DESENHAR BOTÃO
// ==================================================

bool desenharBotao(
    const char *texto,
    Rectangle botao
)
{
    Vector2 mouse = GetMousePosition();

    bool mouseSobre = CheckCollisionPointRec(mouse, botao);

    Color cor;

    if (mouseSobre)
        cor = DARKGRAY;
    else
        cor = GRAY;

    DrawRectangleRec(botao, cor);
    DrawRectangleLinesEx(botao, 2, BLACK);

    int tamanhoTexto = 18;

    int larguraTexto = MeasureText(texto, tamanhoTexto);

    int textoX = botao.x + (botao.width - larguraTexto) / 2;
    int textoY = botao.y + (botao.height - tamanhoTexto) / 2;

    DrawText(texto, textoX, textoY, tamanhoTexto, WHITE);

    return mouseSobre;
}


// ==================================================
// DESENHAR BOTÃO DESABILITADO (não clicável)
// ==================================================

void desenharBotaoDesabilitado(const char *texto, Rectangle botao)
{
    DrawRectangleRec(botao, LIGHTGRAY);
    DrawRectangleLinesEx(botao, 2, GRAY);

    int tamanhoTexto = 18;
    int larguraTexto = MeasureText(texto, tamanhoTexto);

    int textoX = botao.x + (botao.width - larguraTexto) / 2;
    int textoY = botao.y + (botao.height - tamanhoTexto) / 2;

    DrawText(texto, textoX, textoY, tamanhoTexto, GRAY);
}


// ==================================================
// DESENHAR BOTÃO BLOQUEADO (recusado recentemente, em cooldown)
// ==================================================

void desenharBotaoBloqueado(const char *texto, Rectangle botao)
{
    DrawRectangleRec(botao, MAROON);
    DrawRectangleLinesEx(botao, 2, Fade(BLACK, 0.6f));

    int tamanhoTexto = 18;
    int larguraTexto = MeasureText(texto, tamanhoTexto);

    int textoX = botao.x + (botao.width - larguraTexto) / 2;
    int textoY = botao.y + (botao.height - tamanhoTexto) / 2;

    DrawText(texto, textoX, textoY, tamanhoTexto, Fade(WHITE, 0.75f));
}


// ==================================================
// MODO DEV - CONTROLE DE UM STATUS (botão -/+ e valor)
// ==================================================
// Auto-contido (lê mouse/clique internamente, como desenharBotao já faz),
// pra poder ser chamado direto na seção de desenho do painel de dev.

void controleDevStat(
    const char *nome,
    int *valor,
    int minimo,
    int maximo,
    int passo,
    Rectangle botaoMenos,
    Rectangle botaoMais,
    int x,
    int y
)
{
    Vector2 mouse = GetMousePosition();
    bool clique = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);

    if (clique && CheckCollisionPointRec(mouse, botaoMenos))
    {
        *valor -= passo;
        if (*valor < minimo) *valor = minimo;
    }

    if (clique && CheckCollisionPointRec(mouse, botaoMais))
    {
        *valor += passo;
        if (*valor > maximo) *valor = maximo;
    }

    desenharBotao("-", botaoMenos);
    desenharBotao("+", botaoMais);

    DrawText(TextFormat("%s: %d", nome, *valor), x, y, 18, WHITE);
}


// ==================================================
// CARREGAR ASSET (TEXTURA / SOM / MÚSICA)
// ==================================================
// Mesmo asset pode estar em pastas diferentes dependendo de como o jogo foi
// empacotado/executado — tenta alguns caminhos plausíveis relativos à pasta
// "pasta" (ex: "assets", "audio").

void construirCaminhosDoAsset(const char *pasta, const char *arquivo, char caminhos[3][512], int *quantidade)
{
    *quantidade = 0;

    const char *diretorioAtual = GetWorkingDirectory();

    if (diretorioAtual != NULL && diretorioAtual[0] != '\0')
    {
        int escrito = snprintf(caminhos[*quantidade], sizeof(caminhos[0]), "%s/%s/%s", diretorioAtual, pasta, arquivo);

        if (escrito >= 0 && escrito < (int)sizeof(caminhos[0]))
            (*quantidade)++;
    }

    int escrito = snprintf(caminhos[*quantidade], sizeof(caminhos[0]), "%s%s/%s", GetApplicationDirectory(), pasta, arquivo);

    if (escrito >= 0 && escrito < (int)sizeof(caminhos[0]))
        (*quantidade)++;

    escrito = snprintf(caminhos[*quantidade], sizeof(caminhos[0]), "%s../%s/%s", GetApplicationDirectory(), pasta, arquivo);

    if (escrito >= 0 && escrito < (int)sizeof(caminhos[0]))
        (*quantidade)++;
}

Texture2D carregarTexturaDoAsset(const char *arquivo)
{
    char caminhos[3][512];
    int quantidade;
    construirCaminhosDoAsset("assets", arquivo, caminhos, &quantidade);

    for (int i = 0; i < quantidade; ++i)
    {
        Texture2D textura = LoadTexture(caminhos[i]);

        if (textura.id != 0)
            return textura;

        TraceLog(LOG_WARNING, "Falha ao carregar: %s", caminhos[i]);
    }

    return (Texture2D){0};
}

Sound carregarSomDoAsset(const char *arquivo)
{
    char caminhos[3][512];
    int quantidade;
    construirCaminhosDoAsset("audio", arquivo, caminhos, &quantidade);

    for (int i = 0; i < quantidade; ++i)
    {
        Sound som = LoadSound(caminhos[i]);

        if (IsSoundValid(som))
            return som;

        TraceLog(LOG_WARNING, "Falha ao carregar som: %s", caminhos[i]);
    }

    return (Sound){0};
}

Music carregarMusicaDoAsset(const char *arquivo)
{
    char caminhos[3][512];
    int quantidade;
    construirCaminhosDoAsset("audio", arquivo, caminhos, &quantidade);

    for (int i = 0; i < quantidade; ++i)
    {
        Music musica = LoadMusicStream(caminhos[i]);

        if (IsMusicValid(musica))
            return musica;

        TraceLog(LOG_WARNING, "Falha ao carregar musica: %s", caminhos[i]);
    }

    return (Music){0};
}


// ==================================================
// MAIN
// ==================================================

int main(void)
{
    // ==================================================
    // CONFIGURAÇÕES
    // ==================================================

    const int largura = 1100;
    const int altura = 750;

    SetRandomSeed((unsigned int)time(NULL));


    // ==================================================
    // ESTADO GERAL DO JOGO
    // ==================================================

    Pet pet;
    memset(&pet, 0, sizeof(Pet));

    EstadoJogo estado = ESTADO_MENU;

    bool saveExiste = false; // recalculado após InitWindow, pois depende de GetApplicationDirectory

    HistoricoPet historico[MAX_HISTORICO];
    int numHistorico = 0;
    int paginaHistorico = 0;


    // ==================================================
    // MODO DEV (F1) — acelerar o tempo e manipular status pra testar
    // ==================================================

    bool modoDev = false;
    int velocidadeDevIndice = 0;
    float velocidadesDev[] = { 1.0f, 10.0f, 60.0f, 300.0f, 1800.0f };
    int numVelocidadesDev = 5;

    int *statPonteiros[7] = {
        &pet.fome, &pet.felicidade, &pet.energia,
        &pet.saude, &pet.disciplina, &pet.peso, &pet.nivel
    };
    const char *statNomesDev[7] = {
        "Fome", "Felicidade", "Energia", "Saude", "Disciplina", "Peso", "Nivel"
    };
    int statMinDev[7] = { 0, 0, 0, 0, 0, 10, 1 };
    int statMaxDev[7] = { 100, 100, 100, 100, 100, 200, 999 };
    int statPassoDev[7] = { 10, 10, 10, 10, 10, 10, 1 };


    // ==================================================
    // NOMEAÇÃO DO PET
    // ==================================================

    char nomeDigitado[NOME_MAX] = "";
    int letraCount = 0;
    int horaEscolhida = obterHoraLocalAtual(); // "que horas são?" na criação do pet


    // ==================================================
    // CRIAR JANELA
    // ==================================================

    InitWindow(largura, altura, "Meu Pet Virtual");
    InitAudioDevice();

    SetTargetFPS(60);

    saveExiste = arquivoDeSaveExiste();

    // Carrega o histórico (e o pet salvo) desde já, para que "PETS DESCOBERTOS"
    // funcione no menu mesmo antes de o jogador clicar em "CARREGAR JOGO".
    if (saveExiste)
        carregarJogo(&pet, historico, &numHistorico);


    // ==================================================
    // CARREGAR SPRITES
    // ==================================================

    Texture2D texturasEstagio[ESTAGIO_TOTAL] = {0};

    texturasEstagio[ESTAGIO_ADULTO1A] = carregarTexturaDoAsset("adulto1a.png");
    texturasEstagio[ESTAGIO_ADULTO2A] = carregarTexturaDoAsset("adulto2a.png");
    texturasEstagio[ESTAGIO_ADULTO3A] = carregarTexturaDoAsset("adulto3a.png");
    texturasEstagio[ESTAGIO_ADULTO1B] = carregarTexturaDoAsset("adulto1b.png");
    texturasEstagio[ESTAGIO_ADULTO2B] = carregarTexturaDoAsset("adulto2b.png");
    texturasEstagio[ESTAGIO_ADULTO3B] = carregarTexturaDoAsset("adulto3b.png");

    // Bebe, Juvenil1 e Juvenil2 tem sprites de animacao (idle1 / idle2)
    Texture2D bebeIdle1 = carregarTexturaDoAsset("bebe_idle1.png");
    Texture2D bebeIdle2 = carregarTexturaDoAsset("bebe_idle2.png");
    Texture2D juvenil1Idle1 = carregarTexturaDoAsset("juvenil1_idle1.png");
    Texture2D juvenil1Idle2 = carregarTexturaDoAsset("juvenil1_idle2.png");
    Texture2D juvenil2Idle1 = carregarTexturaDoAsset("juvenil2_idle1.png");
    Texture2D juvenil2Idle2 = carregarTexturaDoAsset("juvenil2_idle2.png");


    // ==================================================
    // CARREGAR ÁUDIO
    // ==================================================

    // sonsChoro[indiceSomChoro(estagio)] — adulto1a/1b compartilham som, idem 2a/2b e 3a/3b
    Sound sonsChoro[6];
    sonsChoro[0] = carregarSomDoAsset("baby_cry.ogg");
    sonsChoro[1] = carregarSomDoAsset("juvenil1_cry.ogg");
    sonsChoro[2] = carregarSomDoAsset("juvenil2_cry.ogg");
    sonsChoro[3] = carregarSomDoAsset("adulto1ab_cry.wav");
    sonsChoro[4] = carregarSomDoAsset("adulto2ab_cry.wav");
    sonsChoro[5] = carregarSomDoAsset("adulto3ab_cry.wav");

    Sound somComer = carregarSomDoAsset("eating.mp3");
    Sound somBrincar = carregarSomDoAsset("playing.mp3");

    Music musicaFundo = carregarMusicaDoAsset("background_music.mp3");
    SetMusicVolume(musicaFundo, 0.15f); // bem baixinho, é só ambiente
    PlayMusicStream(musicaFundo);


    // ==================================================
    // ANIMAÇÃO
    // ==================================================

    float tempoAnimacao = 0.0f;
    int framePet = 0;

    int centroPetX = 500;
    int centroPetY = 420;


    // ==================================================
    // RELÓGIOS DE SIMULAÇÃO
    // ==================================================

    float relogioTick = 0.0f;
    float relogioIdade = 0.0f;

    float relogioCoco = 0.0f;
    float proximoCoco = (float)GetRandomValue((int)COCO_INTERVALO_MIN, (int)COCO_INTERVALO_MAX);

    float relogioAtencao = 0.0f;
    float proximaAtencao = (float)GetRandomValue(20, 35);

    float relogioChoro = 0.0f; // repete o choro a cada 3s reais enquanto pede atenção

    float relogioAutosave = 0.0f;

    float acumuladorLuz = 0.0f;

    bool luzAcesa = true;

    float mensagemSalvoTimer = 0.0f;

    float mensagemAcaoTimer = 0.0f;
    char mensagemAcaoTexto[100] = "";

    // Tempo restante de bloqueio de cada botão depois de uma recusa
    float cooldownRefeicao = 0.0f;
    float cooldownPetisco = 0.0f;
    float cooldownBrincar = 0.0f;
    float cooldownRemedio = 0.0f;

    float bannerEvolucaoTimer = 0.0f;
    char bannerEvolucaoTexto[100] = "";

    char popupAdultoTexto[100] = "";


    // ==================================================
    // BOTÕES - TELA DE MENU
    // ==================================================

    Rectangle botaoNovoJogo      = { largura / 2 - 140, 330, 280, 55 };
    Rectangle botaoCarregarJogo  = { largura / 2 - 140, 400, 280, 55 };
    Rectangle botaoHistoricoMenu = { largura / 2 - 140, 470, 280, 55 };


    // ==================================================
    // BOTÕES - POPUP "VIROU ADULTO"
    // ==================================================

    Rectangle botaoPopupNovoPet   = { largura / 2 - 290, 360, 270, 55 };
    Rectangle botaoPopupContinuar = { largura / 2 + 20,  360, 270, 55 };


    // ==================================================
    // BOTÕES - TELA DE HISTÓRICO
    // ==================================================

    Rectangle botaoHistoricoAnterior = { largura / 2 - 300, 680, 140, 45 };
    Rectangle botaoHistoricoProxima  = { largura / 2 + 160, 680, 140, 45 };
    Rectangle botaoHistoricoVoltar   = { largura / 2 - 70,  680, 140, 45 };


    // ==================================================
    // BOTÕES - TELA DE NOMEAR
    // ==================================================

    Rectangle caixaNome        = { largura / 2 - 200, 300, 400, 50 };
    Rectangle botaoHoraMenos   = { largura / 2 - 90,  375, 45,  40 };
    Rectangle botaoHoraMais    = { largura / 2 + 45,  375, 45,  40 };
    Rectangle botaoComecar     = { largura / 2 - 140, 440, 280, 55 };
    Rectangle botaoVoltarMenu  = { largura / 2 - 140, 510, 280, 55 };


    // ==================================================
    // BOTÕES - TELA DE JOGO
    // ==================================================

    int botaoX = 860;
    int botaoLargura = 200;
    int botaoAltura = 45;
    int botaoEspacamento = 55;
    int botaoTopo = 120;

    Rectangle botaoRefeicao   = { botaoX, botaoTopo + botaoEspacamento * 0, botaoLargura, botaoAltura };
    Rectangle botaoPetisco    = { botaoX, botaoTopo + botaoEspacamento * 1, botaoLargura, botaoAltura };
    Rectangle botaoBrincar    = { botaoX, botaoTopo + botaoEspacamento * 2, botaoLargura, botaoAltura };
    Rectangle botaoDormir     = { botaoX, botaoTopo + botaoEspacamento * 3, botaoLargura, botaoAltura };
    Rectangle botaoRemedio    = { botaoX, botaoTopo + botaoEspacamento * 4, botaoLargura, botaoAltura };
    Rectangle botaoElogiar    = { botaoX, botaoTopo + botaoEspacamento * 5, botaoLargura, botaoAltura };
    Rectangle botaoRepreender = { botaoX, botaoTopo + botaoEspacamento * 6, botaoLargura, botaoAltura };
    Rectangle botaoLuz        = { botaoX, botaoTopo + botaoEspacamento * 7, botaoLargura, botaoAltura };
    Rectangle botaoSalvar     = { botaoX, botaoTopo + botaoEspacamento * 8, botaoLargura, botaoAltura };

    Rectangle botaoMenuTopo = { largura - 150, 30, 110, 35 };


    // ==================================================
    // PAINEL - MODO DEV
    // ==================================================

    Rectangle painelDev = { 40, 95, 680, 615 };

    Rectangle botaoDevVelMenos = { painelDev.x + 380, painelDev.y + 40, 40, 32 };
    Rectangle botaoDevVelMais  = { painelDev.x + 430, painelDev.y + 40, 40, 32 };

    Rectangle botaoDevStatMenos[7];
    Rectangle botaoDevStatMais[7];

    for (int i = 0; i < 7; i++)
    {
        int yLinha = painelDev.y + 90 + i * 42;
        botaoDevStatMenos[i] = (Rectangle){ painelDev.x + 380, yLinha, 40, 32 };
        botaoDevStatMais[i]  = (Rectangle){ painelDev.x + 430, yLinha, 40, 32 };
    }

    Rectangle botaoDevDoente     = { painelDev.x + 15,  painelDev.y + 480, 150, 36 };
    Rectangle botaoDevAvancarDia = { painelDev.x + 180, painelDev.y + 480, 170, 36 };
    Rectangle botaoDevHoraMenos  = { painelDev.x + 365, painelDev.y + 480, 80,  36 };
    Rectangle botaoDevHoraMais   = { painelDev.x + 455, painelDev.y + 480, 80,  36 };


    // ==================================================
    // BOTÕES - TELA DE MORTE
    // ==================================================

    Rectangle botaoNovoJogoMorte = { largura / 2 - 140, 420, 280, 55 };
    Rectangle botaoSairMorte     = { largura / 2 - 140, 490, 280, 55 };


    // ==================================================
    // GAME LOOP
    // ==================================================

    while (!WindowShouldClose())
    {
        float dt = GetFrameTime();

        UpdateMusicStream(musicaFundo); // precisa rodar sempre, em qualquer tela

        Vector2 mouse = GetMousePosition();
        bool cliquePressionado = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);


        // ==================================================
        // ESTADO: MENU INICIAL
        // ==================================================

        if (estado == ESTADO_MENU)
        {
            if (cliquePressionado)
            {
                if (CheckCollisionPointRec(mouse, botaoNovoJogo))
                {
                    nomeDigitado[0] = '\0';
                    letraCount = 0;
                    horaEscolhida = obterHoraLocalAtual();
                    estado = ESTADO_NOMEAR;
                }

                if (saveExiste && CheckCollisionPointRec(mouse, botaoCarregarJogo))
                {
                    if (carregarJogo(&pet, historico, &numHistorico))
                    {
                        // reinicia os relógios de simulação para a nova sessão
                        relogioTick = 0.0f;
                        relogioIdade = 0.0f;
                        relogioCoco = 0.0f;
                        proximoCoco = (float)GetRandomValue((int)COCO_INTERVALO_MIN, (int)COCO_INTERVALO_MAX);
                        relogioAtencao = 0.0f;
                        proximaAtencao = calcularProximaAtencao(pet.disciplina);
                        relogioAutosave = 0.0f;
                        acumuladorLuz = 0.0f;
                        luzAcesa = true;
                        bannerEvolucaoTimer = 0.0f;
                        cooldownRefeicao = cooldownPetisco = cooldownBrincar = cooldownRemedio = 0.0f;

                        // Simula o tempo real que passou desde o último save (o
                        // jogo "correndo" enquanto o app estava fechado).
                        bool tornouAdultoOffline = false;

                        if (pet.vivo)
                        {
                            double segundosOffline = (double)(time(NULL) - (time_t)pet.ultimoSalvamento);
                            simularTempoOffline(&pet, historico, &numHistorico, segundosOffline, &tornouAdultoOffline);
                        }

                        if (!pet.vivo)
                        {
                            estado = ESTADO_MORTE;
                        }
                        else if (tornouAdultoOffline)
                        {
                            snprintf(popupAdultoTexto, sizeof(popupAdultoTexto), "Parabens! %s agora e um adulto!", pet.nome);
                            estado = ESTADO_ADULTO_POPUP;
                        }
                        else
                        {
                            estado = ESTADO_JOGO;
                        }

                        salvarJogo(&pet, historico, numHistorico); // persiste o resultado do tempo offline
                    }
                }

                if (CheckCollisionPointRec(mouse, botaoHistoricoMenu))
                {
                    paginaHistorico = 0;
                    estado = ESTADO_HISTORICO;
                }
            }
        }


        // ==================================================
        // ESTADO: PETS DESCOBERTOS (HISTÓRICO)
        // ==================================================

        else if (estado == ESTADO_HISTORICO)
        {
            if (cliquePressionado)
            {
                const int itensPorPagina = 6;
                int totalPaginas = (numHistorico + itensPorPagina - 1) / itensPorPagina;
                if (totalPaginas < 1) totalPaginas = 1;

                if (CheckCollisionPointRec(mouse, botaoHistoricoAnterior) && paginaHistorico > 0)
                    paginaHistorico--;

                if (CheckCollisionPointRec(mouse, botaoHistoricoProxima) && paginaHistorico < totalPaginas - 1)
                    paginaHistorico++;

                if (CheckCollisionPointRec(mouse, botaoHistoricoVoltar))
                    estado = ESTADO_MENU;
            }
        }


        // ==================================================
        // ESTADO: NOMEAR PET
        // ==================================================

        else if (estado == ESTADO_NOMEAR)
        {
            int tecla = GetCharPressed();

            while (tecla > 0)
            {
                if ((tecla >= 32) && (tecla <= 125) && (letraCount < NOME_MAX - 1))
                {
                    nomeDigitado[letraCount] = (char)tecla;
                    letraCount++;
                    nomeDigitado[letraCount] = '\0';
                }

                tecla = GetCharPressed();
            }

            if (IsKeyPressed(KEY_BACKSPACE) && letraCount > 0)
            {
                letraCount--;
                nomeDigitado[letraCount] = '\0';
            }

            if (cliquePressionado)
            {
                if (CheckCollisionPointRec(mouse, botaoHoraMenos))
                    horaEscolhida = (horaEscolhida + 23) % 24;

                if (CheckCollisionPointRec(mouse, botaoHoraMais))
                    horaEscolhida = (horaEscolhida + 1) % 24;
            }

            bool podeComecar = (letraCount > 0);

            if ((cliquePressionado && podeComecar && CheckCollisionPointRec(mouse, botaoComecar)) ||
                (podeComecar && IsKeyPressed(KEY_ENTER)))
            {
                iniciarNovoJogo(&pet, nomeDigitado, horaEscolhida);

                relogioTick = 0.0f;
                relogioIdade = 0.0f;
                relogioCoco = 0.0f;
                proximoCoco = (float)GetRandomValue((int)COCO_INTERVALO_MIN, (int)COCO_INTERVALO_MAX);
                relogioAtencao = 0.0f;
                proximaAtencao = calcularProximaAtencao(pet.disciplina);
                relogioAutosave = 0.0f;
                acumuladorLuz = 0.0f;
                luzAcesa = true;
                bannerEvolucaoTimer = 0.0f;
                cooldownRefeicao = cooldownPetisco = cooldownBrincar = cooldownRemedio = 0.0f;

                salvarJogo(&pet, historico, numHistorico);
                saveExiste = true;

                estado = ESTADO_JOGO;
            }

            if (cliquePressionado && CheckCollisionPointRec(mouse, botaoVoltarMenu))
            {
                estado = ESTADO_MENU;
            }
        }


        // ==================================================
        // ESTADO: JOGO
        // ==================================================

        else if (estado == ESTADO_JOGO)
        {
            // ---------- MODO DEV ----------

            if (IsKeyPressed(KEY_F1))
                modoDev = !modoDev;

            // dtSimulado é o "dt de jogo": igual ao dt real, exceto que o modo
            // dev pode multiplicá-lo pra acelerar o tempo. epochCriacao é
            // deslocado pelo tempo "extra" pra a hora-do-dia do pet acompanhar.
            float dtSimulado = dt;

            if (modoDev)
            {
                dtSimulado = dt * velocidadesDev[velocidadeDevIndice];
                pet.epochCriacao -= (long long)(dtSimulado - dt);
            }

            // ---------- ANIMAÇÃO ----------

            tempoAnimacao += dt;

            if (tempoAnimacao >= 0.5f)
            {
                tempoAnimacao = 0.0f;
                framePet = (framePet == 0) ? 1 : 0;
            }

            // ---------- NECESSIDADES + AMOSTRAGEM PARA EVOLUÇÃO ----------

            relogioTick += dtSimulado;

            if (relogioTick >= INTERVALO_TICK)
            {
                relogioTick = 0.0f;
                executarTick(&pet, historico, &numHistorico);
            }

            // ---------- ENVELHECIMENTO / NÍVEL / EVOLUÇÃO ----------

            relogioIdade += dtSimulado;

            if (relogioIdade >= INTERVALO_DIA && pet.vivo)
            {
                relogioIdade = 0.0f;

                char mensagemEvolucao[100];
                bool tornouAdulto = executarAvancoDeDia(&pet, historico, &numHistorico, mensagemEvolucao, sizeof(mensagemEvolucao));

                if (tornouAdulto)
                {
                    snprintf(popupAdultoTexto, sizeof(popupAdultoTexto), "Parabens! %s agora e um adulto!", pet.nome);
                    estado = ESTADO_ADULTO_POPUP;
                }
                else if (mensagemEvolucao[0] != '\0')
                {
                    strncpy(bannerEvolucaoTexto, mensagemEvolucao, sizeof(bannerEvolucaoTexto) - 1);
                    bannerEvolucaoTexto[sizeof(bannerEvolucaoTexto) - 1] = '\0';
                    bannerEvolucaoTimer = 5.0f;
                }
            }

            // ---------- LUZ ----------
            // (a recuperação de energia e o acordar automático agora ficam
            // dentro de executarTick, junto com o resto das necessidades —
            // assim funcionam também durante o tempo offline)

            if (pet.dormindo && luzAcesa)
            {
                acumuladorLuz += dtSimulado;

                if (acumuladorLuz >= 1800.0f) // dormir de luz acesa incomoda, a cada 30 min
                {
                    acumuladorLuz = 0.0f;
                    pet.felicidade -= 2;
                    limitarStatus(&pet);
                }
            }

            // ---------- COCÔ ----------

            relogioCoco += dtSimulado;

            if (relogioCoco >= proximoCoco)
            {
                relogioCoco = 0.0f;
                proximoCoco = (float)GetRandomValue((int)COCO_INTERVALO_MIN, (int)COCO_INTERVALO_MAX);
                adicionarCoco(&pet);
            }

            // ---------- CHAMADO DE ATENÇÃO ----------

            relogioAtencao += dtSimulado;

            if (relogioAtencao >= proximaAtencao && !pet.pedindoAtencao && !pet.dormindo)
            {
                relogioAtencao = 0.0f;
                proximaAtencao = calcularProximaAtencao(pet.disciplina);

                pet.pedindoAtencao = true;
                pet.atencaoPorNecessidade = precisaCuidado(&pet); // fixado agora, não recalculado depois

                relogioChoro = 0.0f;
                PlaySound(sonsChoro[indiceSomChoro(pet.estagio)]);
            }

            // Enquanto estiver pedindo atenção, repete o choro a cada 3s
            // reais (não usa dtSimulado — o modo dev não deve acelerar o som).
            if (pet.pedindoAtencao)
            {
                relogioChoro += dt;

                if (relogioChoro >= 3.0f)
                {
                    relogioChoro -= 3.0f;
                    PlaySound(sonsChoro[indiceSomChoro(pet.estagio)]);
                }
            }
            else
            {
                relogioChoro = 0.0f;
            }

            // ---------- AUTOSAVE ----------

            relogioAutosave += dt;

            if (relogioAutosave >= INTERVALO_AUTOSAVE)
            {
                relogioAutosave = 0.0f;
                salvarJogo(&pet, historico, numHistorico);
            }

            if (mensagemSalvoTimer > 0.0f)
                mensagemSalvoTimer -= dt;

            if (mensagemAcaoTimer > 0.0f)
                mensagemAcaoTimer -= dt;

            if (cooldownRefeicao > 0.0f) cooldownRefeicao -= dtSimulado;
            if (cooldownPetisco  > 0.0f) cooldownPetisco  -= dtSimulado;
            if (cooldownBrincar  > 0.0f) cooldownBrincar  -= dtSimulado;
            if (cooldownRemedio  > 0.0f) cooldownRemedio  -= dtSimulado;

            if (bannerEvolucaoTimer > 0.0f)
                bannerEvolucaoTimer -= dt;

            // ---------- MORTE ----------
            // (o arquivamento em "pets descobertos" já é feito dentro de executarTick)

            if (!pet.vivo)
            {
                salvarJogo(&pet, historico, numHistorico);
                estado = ESTADO_MORTE;
            }

            // ---------- CLIQUES ----------

            if (cliquePressionado)
            {
                if (CheckCollisionPointRec(mouse, botaoRefeicao) && cooldownRefeicao <= 0.0f)
                {
                    if (darRefeicao(&pet))
                        PlaySound(somComer);
                    else
                    {
                        snprintf(mensagemAcaoTexto, sizeof(mensagemAcaoTexto), "%s recusou a refeicao!", pet.nome);
                        mensagemAcaoTimer = 2.0f;
                        cooldownRefeicao = COOLDOWN_RECUSA_SEGUNDOS;
                    }
                }

                if (CheckCollisionPointRec(mouse, botaoPetisco) && cooldownPetisco <= 0.0f)
                {
                    if (darPetisco(&pet))
                        PlaySound(somComer);
                    else
                    {
                        snprintf(mensagemAcaoTexto, sizeof(mensagemAcaoTexto), "%s recusou o petisco!", pet.nome);
                        mensagemAcaoTimer = 2.0f;
                        cooldownPetisco = COOLDOWN_RECUSA_SEGUNDOS;
                    }
                }

                if (CheckCollisionPointRec(mouse, botaoBrincar) && cooldownBrincar <= 0.0f)
                {
                    if (brincar(&pet))
                        PlaySound(somBrincar);
                    else
                    {
                        snprintf(mensagemAcaoTexto, sizeof(mensagemAcaoTexto), "%s nao quis brincar agora!", pet.nome);
                        mensagemAcaoTimer = 2.0f;
                        cooldownBrincar = COOLDOWN_RECUSA_SEGUNDOS;
                    }
                }

                if (CheckCollisionPointRec(mouse, botaoDormir))
                    alternarDormir(&pet);

                if (CheckCollisionPointRec(mouse, botaoRemedio) && cooldownRemedio <= 0.0f && !darRemedio(&pet) && pet.doente)
                {
                    snprintf(mensagemAcaoTexto, sizeof(mensagemAcaoTexto), "%s recusou o remedio!", pet.nome);
                    mensagemAcaoTimer = 2.0f;
                    cooldownRemedio = COOLDOWN_RECUSA_SEGUNDOS;
                }

                if (CheckCollisionPointRec(mouse, botaoElogiar))
                    elogiar(&pet);

                if (CheckCollisionPointRec(mouse, botaoRepreender))
                    repreender(&pet);

                if (CheckCollisionPointRec(mouse, botaoLuz))
                    luzAcesa = !luzAcesa;

                if (CheckCollisionPointRec(mouse, botaoSalvar))
                {
                    salvarJogo(&pet, historico, numHistorico);
                    saveExiste = true;
                    mensagemSalvoTimer = 2.0f;
                }

                if (CheckCollisionPointRec(mouse, botaoMenuTopo))
                {
                    salvarJogo(&pet, historico, numHistorico);
                    saveExiste = true;
                    estado = ESTADO_MENU;
                }

                if (modoDev)
                {
                    if (CheckCollisionPointRec(mouse, botaoDevVelMenos))
                        velocidadeDevIndice = (velocidadeDevIndice + numVelocidadesDev - 1) % numVelocidadesDev;

                    if (CheckCollisionPointRec(mouse, botaoDevVelMais))
                        velocidadeDevIndice = (velocidadeDevIndice + 1) % numVelocidadesDev;

                    if (CheckCollisionPointRec(mouse, botaoDevDoente))
                        pet.doente = !pet.doente;

                    if (CheckCollisionPointRec(mouse, botaoDevAvancarDia))
                    {
                        char mensagemDevDescartavel[100];
                        bool tornouAdultoDev = executarAvancoDeDia(&pet, historico, &numHistorico, mensagemDevDescartavel, sizeof(mensagemDevDescartavel));

                        if (tornouAdultoDev)
                        {
                            snprintf(popupAdultoTexto, sizeof(popupAdultoTexto), "Parabens! %s agora e um adulto!", pet.nome);
                            estado = ESTADO_ADULTO_POPUP;
                        }
                    }

                    if (CheckCollisionPointRec(mouse, botaoDevHoraMenos))
                        pet.epochCriacao += 3600;

                    if (CheckCollisionPointRec(mouse, botaoDevHoraMais))
                        pet.epochCriacao -= 3600;
                }

                // Limpar cocô clicando diretamente nele — só com a luz acesa
                // (com a luz apagada o cocô fica só invisível, não sumiu de
                // verdade, então não pode ser limpo sem querer no escuro)
                if (luzAcesa)
                {
                    for (int i = 0; i < pet.numCocos; i++)
                    {
                        float distX = mouse.x - pet.cocos[i].x;
                        float distY = mouse.y - pet.cocos[i].y;
                        float distancia = (distX * distX) + (distY * distY);

                        if (distancia <= (18 * 18))
                        {
                            limparCoco(&pet, i);
                            break;
                        }
                    }
                }
            }
        }


        // ==================================================
        // ESTADO: POPUP "VIROU ADULTO"
        // ==================================================

        else if (estado == ESTADO_ADULTO_POPUP)
        {
            if (cliquePressionado)
            {
                if (CheckCollisionPointRec(mouse, botaoPopupNovoPet))
                {
                    // O pet já foi arquivado em "pets descobertos" no momento em que virou
                    // adulto; aqui só salvamos e seguimos para criar o próximo pet.
                    salvarJogo(&pet, historico, numHistorico);

                    nomeDigitado[0] = '\0';
                    letraCount = 0;
                    horaEscolhida = obterHoraLocalAtual();
                    estado = ESTADO_NOMEAR;
                }

                if (CheckCollisionPointRec(mouse, botaoPopupContinuar))
                {
                    estado = ESTADO_JOGO;
                }
            }
        }


        // ==================================================
        // ESTADO: MORTE
        // ==================================================

        else if (estado == ESTADO_MORTE)
        {
            if (cliquePressionado)
            {
                if (CheckCollisionPointRec(mouse, botaoNovoJogoMorte))
                {
                    nomeDigitado[0] = '\0';
                    letraCount = 0;
                    horaEscolhida = obterHoraLocalAtual();
                    estado = ESTADO_NOMEAR;
                }

                if (CheckCollisionPointRec(mouse, botaoSairMorte))
                {
                    estado = ESTADO_MENU;
                }
            }
        }


        // ==================================================
        // DESENHO
        // ==================================================

        BeginDrawing();

        ClearBackground(RAYWHITE);


        if (estado == ESTADO_MENU)
        {
            const char *titulo = "MEU PET VIRTUAL";
            int tamTitulo = 44;
            int largTitulo = MeasureText(titulo, tamTitulo);

            DrawText(titulo, largura / 2 - largTitulo / 2, 180, tamTitulo, DARKGRAY);

            desenharBotao("NOVO JOGO", botaoNovoJogo);

            if (saveExiste)
                desenharBotao("CARREGAR JOGO", botaoCarregarJogo);
            else
                desenharBotaoDesabilitado("CARREGAR JOGO", botaoCarregarJogo);

            desenharBotao("PETS DESCOBERTOS", botaoHistoricoMenu);

            if (!saveExiste)
            {
                const char *aviso = "Nenhum save encontrado";
                int largAviso = MeasureText(aviso, 18);
                DrawText(aviso, largura / 2 - largAviso / 2, 550, 18, GRAY);
            }
        }

        else if (estado == ESTADO_HISTORICO)
        {
            desenharHistorico(historico, numHistorico, paginaHistorico, texturasEstagio, largura);

            const int itensPorPagina = 6;
            int totalPaginas = (numHistorico + itensPorPagina - 1) / itensPorPagina;
            if (totalPaginas < 1) totalPaginas = 1;

            if (paginaHistorico > 0)
                desenharBotao("ANTERIOR", botaoHistoricoAnterior);
            else
                desenharBotaoDesabilitado("ANTERIOR", botaoHistoricoAnterior);

            if (paginaHistorico < totalPaginas - 1)
                desenharBotao("PROXIMA", botaoHistoricoProxima);
            else
                desenharBotaoDesabilitado("PROXIMA", botaoHistoricoProxima);

            desenharBotao("VOLTAR", botaoHistoricoVoltar);
        }

        else if (estado == ESTADO_NOMEAR)
        {
            const char *titulo = "COMO SE CHAMA SEU PET?";
            int tamTitulo = 30;
            int largTitulo = MeasureText(titulo, tamTitulo);

            DrawText(titulo, largura / 2 - largTitulo / 2, 250, tamTitulo, DARKGRAY);

            DrawRectangleRec(caixaNome, RAYWHITE);
            DrawRectangleLinesEx(caixaNome, 2, DARKGRAY);

            bool cursorAceso = (((int)(GetTime() * 2.0f)) % 2) == 0;

            const char *textoExibido = TextFormat(
                "%s%s",
                nomeDigitado,
                cursorAceso ? "_" : ""
            );

            DrawText(textoExibido, caixaNome.x + 10, caixaNome.y + 13, 24, DARKGRAY);

            const char *perguntaHora = "Que horas sao agora?";
            int largPerguntaHora = MeasureText(perguntaHora, 18);
            DrawText(perguntaHora, largura / 2 - largPerguntaHora / 2, 350, 18, DARKGRAY);

            desenharBotao("-", botaoHoraMenos);
            desenharBotao("+", botaoHoraMais);

            const char *textoHora = TextFormat("%02dh", horaEscolhida);
            int largTextoHora = MeasureText(textoHora, 26);
            DrawText(textoHora, largura / 2 - largTextoHora / 2, 383, 26, DARKGRAY);

            bool podeComecar = (letraCount > 0);

            if (podeComecar)
                desenharBotao("COMECAR", botaoComecar);
            else
                desenharBotaoDesabilitado("COMECAR", botaoComecar);

            desenharBotao("VOLTAR", botaoVoltarMenu);
        }

        else if (estado == ESTADO_JOGO || estado == ESTADO_ADULTO_POPUP)
        {
            DrawText("Meu Pet Virtual", 50, 40, 40, DARKGRAY);
            DrawText(TextFormat("Nome: %s", pet.nome), 50, 85, 20, DARKGRAY);

            // Com a luz apagada não dá para ver o painel de status nem o cocô no chão
            if (luzAcesa)
            {
                desenharStatus(&pet);
                desenharCocos(&pet);
            }

            desenharPet(&pet, texturasEstagio, bebeIdle1, bebeIdle2, juvenil1Idle1, juvenil1Idle2, juvenil2Idle1, juvenil2Idle2, framePet, centroPetX, centroPetY);

            desenharDoenca(&pet, centroPetX + 90, centroPetY - 120);
            desenharAtencao(&pet, centroPetX - 90, centroPetY - 120);

            if (cooldownRefeicao > 0.0f) desenharBotaoBloqueado("REFEICAO", botaoRefeicao); else desenharBotao("REFEICAO", botaoRefeicao);
            if (cooldownPetisco  > 0.0f) desenharBotaoBloqueado("PETISCO", botaoPetisco);   else desenharBotao("PETISCO", botaoPetisco);
            if (cooldownBrincar  > 0.0f) desenharBotaoBloqueado("BRINCAR", botaoBrincar);   else desenharBotao("BRINCAR", botaoBrincar);
            desenharBotao(pet.dormindo ? "ACORDAR" : "DORMIR", botaoDormir);
            if (cooldownRemedio  > 0.0f) desenharBotaoBloqueado("REMEDIO", botaoRemedio);   else desenharBotao("REMEDIO", botaoRemedio);
            desenharBotao("ELOGIAR", botaoElogiar);
            desenharBotao("REPREENDER", botaoRepreender);
            desenharBotao(luzAcesa ? "APAGAR LUZ" : "ACENDER LUZ", botaoLuz);
            desenharBotao("SALVAR", botaoSalvar);
            desenharBotao("MENU", botaoMenuTopo);

            if (mensagemSalvoTimer > 0.0f)
                DrawText("Jogo salvo!", botaoX, botaoTopo + botaoEspacamento * 9 + 10, 20, DARKGREEN);

            if (mensagemAcaoTimer > 0.0f)
                DrawText(mensagemAcaoTexto, botaoX, botaoTopo + botaoEspacamento * 9 + 35, 18, MAROON);

            if (bannerEvolucaoTimer > 0.0f)
            {
                int largBanner = MeasureText(bannerEvolucaoTexto, 26);

                DrawRectangle(largura / 2 - largBanner / 2 - 20, 90, largBanner + 40, 45, Fade(GOLD, 0.9f));
                DrawRectangleLines(largura / 2 - largBanner / 2 - 20, 90, largBanner + 40, 45, DARKBROWN);
                DrawText(bannerEvolucaoTexto, largura / 2 - largBanner / 2, 100, 26, DARKBROWN);
            }

            // Escurece a tela quando a luz está apagada
            if (!luzAcesa)
            {
                float alpha = pet.dormindo ? 0.75f : 0.55f;
                DrawRectangle(0, 0, largura, altura, Fade(BLACK, alpha));

                // Redesenha os botões por cima do escurecimento para continuarem visíveis/clicáveis
                if (cooldownRefeicao > 0.0f) desenharBotaoBloqueado("REFEICAO", botaoRefeicao); else desenharBotao("REFEICAO", botaoRefeicao);
                if (cooldownPetisco  > 0.0f) desenharBotaoBloqueado("PETISCO", botaoPetisco);   else desenharBotao("PETISCO", botaoPetisco);
                if (cooldownBrincar  > 0.0f) desenharBotaoBloqueado("BRINCAR", botaoBrincar);   else desenharBotao("BRINCAR", botaoBrincar);
                desenharBotao(pet.dormindo ? "ACORDAR" : "DORMIR", botaoDormir);
                if (cooldownRemedio  > 0.0f) desenharBotaoBloqueado("REMEDIO", botaoRemedio);   else desenharBotao("REMEDIO", botaoRemedio);
                desenharBotao("ELOGIAR", botaoElogiar);
                desenharBotao("REPREENDER", botaoRepreender);
                desenharBotao(luzAcesa ? "APAGAR LUZ" : "ACENDER LUZ", botaoLuz);
                desenharBotao("SALVAR", botaoSalvar);
                desenharBotao("MENU", botaoMenuTopo);
            }

            // Popup modal de evolução para adulto (bloqueia a tela de jogo por baixo)
            if (estado == ESTADO_ADULTO_POPUP)
            {
                DrawRectangle(0, 0, largura, altura, Fade(BLACK, 0.6f));

                int tamMsg = 22;
                int largMsg = MeasureText(popupAdultoTexto, tamMsg);

                int largCaixa = largMsg + 80;
                if (largCaixa < 600) largCaixa = 600;

                int xCaixa = largura / 2 - largCaixa / 2;
                int yCaixa = 240;
                int altCaixa = 210;

                DrawRectangle(xCaixa, yCaixa, largCaixa, altCaixa, Fade(GOLD, 0.95f));
                DrawRectangleLines(xCaixa, yCaixa, largCaixa, altCaixa, DARKBROWN);

                DrawText(popupAdultoTexto, largura / 2 - largMsg / 2, yCaixa + 30, tamMsg, DARKBROWN);

                desenharBotao("1 - CRIAR NOVO PET", botaoPopupNovoPet);
                desenharBotao("2 - CONTINUAR", botaoPopupContinuar);
            }

            // ---------- PAINEL DO MODO DEV ----------

            if (modoDev)
            {
                DrawRectangleRec(painelDev, BLACK);
                DrawRectangleLinesEx(painelDev, 2, RED);

                DrawText("MODO DEV (F1 para sair)", painelDev.x + 15, painelDev.y + 10, 22, RED);

                DrawText(
                    TextFormat("Velocidade: %.0fx", velocidadesDev[velocidadeDevIndice]),
                    painelDev.x + 15, painelDev.y + 48, 18, WHITE
                );
                desenharBotao("-", botaoDevVelMenos);
                desenharBotao("+", botaoDevVelMais);

                for (int i = 0; i < 7; i++)
                {
                    controleDevStat(
                        statNomesDev[i], statPonteiros[i],
                        statMinDev[i], statMaxDev[i], statPassoDev[i],
                        botaoDevStatMenos[i], botaoDevStatMais[i],
                        painelDev.x + 15, painelDev.y + 98 + i * 42
                    );
                }

                desenharBotao(pet.doente ? "CURAR" : "ADOECER", botaoDevDoente);
                desenharBotao("AVANCAR 1 DIA", botaoDevAvancarDia);
                desenharBotao("HORA -1h", botaoDevHoraMenos);
                desenharBotao("HORA +1h", botaoDevHoraMais);

                float horaDev = horaAtualDoPet(&pet);

                DrawText(
                    TextFormat(
                        "Hora do pet: %02d:%02dh (%s)%s",
                        (int)horaDev,
                        (int)((horaDev - (int)horaDev) * 60.0f),
                        ehPeriodoNoturno(horaDev) ? "NOITE" : "DIA",
                        pet.sonoProfundo ? " - sono profundo" : ""
                    ),
                    painelDev.x + 15, painelDev.y + 530, 18, YELLOW
                );
            }
        }

        else if (estado == ESTADO_MORTE)
        {
            const char *titulo = TextFormat("%s nao resistiu...", pet.nome);
            int tamTitulo = 34;
            int largTitulo = MeasureText(titulo, tamTitulo);

            DrawText(titulo, largura / 2 - largTitulo / 2, 220, tamTitulo, MAROON);

            const char *subtitulo = TextFormat(
                "Viveu %d dia(s), nivel %d, estagio %s",
                pet.idade,
                pet.nivel,
                nomeDoEstagio(pet.estagio)
            );

            int largSub = MeasureText(subtitulo, 20);
            DrawText(subtitulo, largura / 2 - largSub / 2, 270, 20, DARKGRAY);

            const char *dica = "Negligencia (fome, sujeira ou doenca) pode ser fatal.";
            int largDica = MeasureText(dica, 18);
            DrawText(dica, largura / 2 - largDica / 2, 320, 18, GRAY);

            desenharBotao("NOVO JOGO", botaoNovoJogoMorte);
            desenharBotao("MENU", botaoSairMorte);
        }

        EndDrawing();
    }


    // ==================================================
    // SALVAR ANTES DE SAIR
    // ==================================================

    if (estado == ESTADO_JOGO)
        salvarJogo(&pet, historico, numHistorico);


    // ==================================================
    // LIBERAR MEMÓRIA
    // ==================================================

    for (int i = 0; i < ESTAGIO_TOTAL; i++)
    {
        if (i == ESTAGIO_BEBE || i == ESTAGIO_JUVENIL1 || i == ESTAGIO_JUVENIL2)
            continue; // essas nao usam texturasEstagio, tem suas proprias texturas de animacao

        UnloadTexture(texturasEstagio[i]);
    }

    UnloadTexture(bebeIdle1);
    UnloadTexture(bebeIdle2);
    UnloadTexture(juvenil1Idle1);
    UnloadTexture(juvenil1Idle2);
    UnloadTexture(juvenil2Idle1);
    UnloadTexture(juvenil2Idle2);

    for (int i = 0; i < 6; i++)
        UnloadSound(sonsChoro[i]);

    UnloadSound(somComer);
    UnloadSound(somBrincar);
    UnloadMusicStream(musicaFundo);

    CloseAudioDevice();
    CloseWindow();

    return 0;
}
