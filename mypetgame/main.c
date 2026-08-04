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
// SESSÃO DE JOGO — ESTRUTURAS DE SUPORTE AO LOOP PRINCIPAL
// ==================================================
// Tudo que antes era variável solta dentro de main() agora vive em structs,
// para que as telas (menu, jogo, histórico...) possam ser tratadas por
// funções próprias em vez de um único main() gigante.

// Tempo restante de bloqueio de cada botão depois de uma recusa (ver
// COOLDOWN_RECUSA_SEGUNDOS e chanceDeRecusa).
typedef struct
{
    float refeicao;
    float petisco;
    float brincar;
    float remedio;
} Cooldowns;

// Relógios (em segundos de jogo) que disparam os eventos periódicos da
// simulação ao vivo: pulso de necessidades, avanço de dia, cocô, pedidos de
// atenção, autosave e o "incômodo" de dormir de luz acesa.
typedef struct
{
    float tick;
    float idade;
    float coco;
    float proximoCoco;
    float atencao;
    float proximaAtencao;
    float choro;
    float autosave;
    float acumuladorLuz;
} RelogiosSimulacao;

// Configuração fixa do painel do modo dev (F1): os ponteiros apontam direto
// para os campos do Pet desta sessão, montados uma única vez em main().
typedef struct
{
    int *ponteiros[7];
    const char *nomes[7];
    int minimo[7];
    int maximo[7];
    int passo[7];
    float velocidades[5];
    int numVelocidades;
} PainelDevConfig;

// Botões de cada tela, agrupados do jeito que já estavam comentados no
// código original — só que agora como dados em vez de variáveis soltas.
typedef struct
{
    Rectangle novoJogo;
    Rectangle carregarJogo;
    Rectangle verHistorico;
} BotoesMenu;

typedef struct
{
    Rectangle novoPet;
    Rectangle continuar;
} BotoesPopupAdulto;

typedef struct
{
    Rectangle anterior;
    Rectangle proxima;
    Rectangle voltar;
} BotoesHistorico;

typedef struct
{
    Rectangle caixaNome;
    Rectangle horaMenos;
    Rectangle horaMais;
    Rectangle comecar;
    Rectangle voltar;
} BotoesNomear;

typedef struct
{
    Rectangle refeicao;
    Rectangle petisco;
    Rectangle brincar;
    Rectangle dormir;
    Rectangle remedio;
    Rectangle elogiar;
    Rectangle repreender;
    Rectangle luz;
    Rectangle salvar;
    Rectangle menuTopo;
} BotoesJogo;

typedef struct
{
    Rectangle painel;
    Rectangle velMenos;
    Rectangle velMais;
    Rectangle statMenos[7];
    Rectangle statMais[7];
    Rectangle doente;
    Rectangle avancarDia;
    Rectangle horaMenos;
    Rectangle horaMais;
} BotoesDev;

typedef struct
{
    Rectangle novoJogo;
    Rectangle sair;
} BotoesMorte;

typedef struct
{
    BotoesMenu menu;
    BotoesPopupAdulto popupAdulto;
    BotoesHistorico historico;
    BotoesNomear nomear;
    BotoesJogo jogo;
    BotoesDev dev;
    BotoesMorte morte;
} Botoes;

// Texturas, sons e música — carregados uma vez no início e usados só para
// leitura durante o jogo todo.
typedef struct
{
    Texture2D texturasEstagio[ESTAGIO_TOTAL];
    Texture2D bebeIdle1, bebeIdle2;
    Texture2D juvenil1Idle1, juvenil1Idle2;
    Texture2D juvenil2Idle1, juvenil2Idle2;

    Sound sonsChoro[6];
    Sound somComer;
    Sound somBrincar;
    Music musicaFundo;
} Recursos;

// Todo o estado "ao vivo" da sessão de jogo: o pet, o histórico, em que tela
// o jogador está, e todos os timers/mensagens/flags de UI que antes eram
// variáveis soltas em main().
typedef struct
{
    Pet pet;
    EstadoJogo estado;
    bool saveExiste;

    HistoricoPet historico[MAX_HISTORICO];
    int numHistorico;
    int paginaHistorico;

    bool modoDev;
    int velocidadeDevIndice;

    char nomeDigitado[NOME_MAX];
    int letraCount;
    int horaEscolhida;

    RelogiosSimulacao relogios;

    bool luzAcesa;

    float mensagemSalvoTimer;
    float mensagemAcaoTimer;
    char mensagemAcaoTexto[100];

    Cooldowns cooldowns;

    float bannerEvolucaoTimer;
    char bannerEvolucaoTexto[100];

    char popupAdultoTexto[100];

    float tempoAnimacao;
    int framePet;
} Sessao;


// ==================================================
// HELPERS DO LOOP PRINCIPAL
// ==================================================

// Reinicia os relógios de simulação e a UI de uma sessão de jogo — usado
// tanto ao carregar um save quanto ao começar um pet novo (eram dois blocos
// idênticos duplicados dentro de main()).
void resetarSessaoDeJogo(
    RelogiosSimulacao *relogios,
    bool *luzAcesa,
    float *bannerEvolucaoTimer,
    Cooldowns *cooldowns,
    int disciplina
)
{
    relogios->tick = 0.0f;
    relogios->idade = 0.0f;
    relogios->coco = 0.0f;
    relogios->proximoCoco = (float)GetRandomValue((int)COCO_INTERVALO_MIN, (int)COCO_INTERVALO_MAX);
    relogios->atencao = 0.0f;
    relogios->proximaAtencao = calcularProximaAtencao(disciplina);
    relogios->autosave = 0.0f;
    relogios->acumuladorLuz = 0.0f;

    *luzAcesa = true;
    *bannerEvolucaoTimer = 0.0f;

    cooldowns->refeicao = 0.0f;
    cooldowns->petisco = 0.0f;
    cooldowns->brincar = 0.0f;
    cooldowns->remedio = 0.0f;
}

// Desenha a coluna de botões de ação da tela de jogo — usada tanto no
// desenho normal quanto redesenhada por cima do escurecimento de "luz
// apagada" (eram dois blocos idênticos duplicados dentro de main()).
void desenharBotoesDeAcao(Pet *pet, Cooldowns cooldowns, bool luzAcesa, BotoesJogo botoes)
{
    if (cooldowns.refeicao > 0.0f) desenharBotaoBloqueado("REFEICAO", botoes.refeicao); else desenharBotao("REFEICAO", botoes.refeicao);
    if (cooldowns.petisco  > 0.0f) desenharBotaoBloqueado("PETISCO", botoes.petisco);   else desenharBotao("PETISCO", botoes.petisco);
    if (cooldowns.brincar  > 0.0f) desenharBotaoBloqueado("BRINCAR", botoes.brincar);   else desenharBotao("BRINCAR", botoes.brincar);
    desenharBotao(pet->dormindo ? "ACORDAR" : "DORMIR", botoes.dormir);
    if (cooldowns.remedio  > 0.0f) desenharBotaoBloqueado("REMEDIO", botoes.remedio);   else desenharBotao("REMEDIO", botoes.remedio);
    desenharBotao("ELOGIAR", botoes.elogiar);
    desenharBotao("REPREENDER", botoes.repreender);
    desenharBotao(luzAcesa ? "APAGAR LUZ" : "ACENDER LUZ", botoes.luz);
    desenharBotao("SALVAR", botoes.salvar);
    desenharBotao("MENU", botoes.menuTopo);
}


// ==================================================
// TELAS — ATUALIZAR (INPUT + LÓGICA)
// ==================================================

void atualizarMenu(Sessao *s, Botoes *botoes, Vector2 mouse, bool clique)
{
    if (!clique) return;

    if (CheckCollisionPointRec(mouse, botoes->menu.novoJogo))
    {
        s->nomeDigitado[0] = '\0';
        s->letraCount = 0;
        s->horaEscolhida = obterHoraLocalAtual();
        s->estado = ESTADO_NOMEAR;
    }

    if (s->saveExiste && CheckCollisionPointRec(mouse, botoes->menu.carregarJogo))
    {
        if (carregarJogo(&s->pet, s->historico, &s->numHistorico))
        {
            resetarSessaoDeJogo(&s->relogios, &s->luzAcesa, &s->bannerEvolucaoTimer, &s->cooldowns, s->pet.disciplina);

            // Simula o tempo real que passou desde o último save (o jogo
            // "correndo" enquanto o app estava fechado).
            bool tornouAdultoOffline = false;

            if (s->pet.vivo)
            {
                double segundosOffline = (double)(time(NULL) - (time_t)s->pet.ultimoSalvamento);
                simularTempoOffline(&s->pet, s->historico, &s->numHistorico, segundosOffline, &tornouAdultoOffline);
            }

            if (!s->pet.vivo)
            {
                s->estado = ESTADO_MORTE;
            }
            else if (tornouAdultoOffline)
            {
                snprintf(s->popupAdultoTexto, sizeof(s->popupAdultoTexto), "Parabens! %s agora e um adulto!", s->pet.nome);
                s->estado = ESTADO_ADULTO_POPUP;
            }
            else
            {
                s->estado = ESTADO_JOGO;
            }

            salvarJogo(&s->pet, s->historico, s->numHistorico); // persiste o resultado do tempo offline
        }
    }

    if (CheckCollisionPointRec(mouse, botoes->menu.verHistorico))
    {
        s->paginaHistorico = 0;
        s->estado = ESTADO_HISTORICO;
    }
}

void atualizarHistorico(Sessao *s, Botoes *botoes, Vector2 mouse, bool clique)
{
    if (!clique) return;

    const int itensPorPagina = 6;
    int totalPaginas = (s->numHistorico + itensPorPagina - 1) / itensPorPagina;
    if (totalPaginas < 1) totalPaginas = 1;

    if (CheckCollisionPointRec(mouse, botoes->historico.anterior) && s->paginaHistorico > 0)
        s->paginaHistorico--;

    if (CheckCollisionPointRec(mouse, botoes->historico.proxima) && s->paginaHistorico < totalPaginas - 1)
        s->paginaHistorico++;

    if (CheckCollisionPointRec(mouse, botoes->historico.voltar))
        s->estado = ESTADO_MENU;
}

void atualizarNomear(Sessao *s, Botoes *botoes, Vector2 mouse, bool clique)
{
    int tecla = GetCharPressed();

    while (tecla > 0)
    {
        if ((tecla >= 32) && (tecla <= 125) && (s->letraCount < NOME_MAX - 1))
        {
            s->nomeDigitado[s->letraCount] = (char)tecla;
            s->letraCount++;
            s->nomeDigitado[s->letraCount] = '\0';
        }

        tecla = GetCharPressed();
    }

    if (IsKeyPressed(KEY_BACKSPACE) && s->letraCount > 0)
    {
        s->letraCount--;
        s->nomeDigitado[s->letraCount] = '\0';
    }

    if (clique)
    {
        if (CheckCollisionPointRec(mouse, botoes->nomear.horaMenos))
            s->horaEscolhida = (s->horaEscolhida + 23) % 24;

        if (CheckCollisionPointRec(mouse, botoes->nomear.horaMais))
            s->horaEscolhida = (s->horaEscolhida + 1) % 24;
    }

    bool podeComecar = (s->letraCount > 0);

    if ((clique && podeComecar && CheckCollisionPointRec(mouse, botoes->nomear.comecar)) ||
        (podeComecar && IsKeyPressed(KEY_ENTER)))
    {
        iniciarNovoJogo(&s->pet, s->nomeDigitado, s->horaEscolhida);

        resetarSessaoDeJogo(&s->relogios, &s->luzAcesa, &s->bannerEvolucaoTimer, &s->cooldowns, s->pet.disciplina);

        salvarJogo(&s->pet, s->historico, s->numHistorico);
        s->saveExiste = true;

        s->estado = ESTADO_JOGO;
    }

    if (clique && CheckCollisionPointRec(mouse, botoes->nomear.voltar))
    {
        s->estado = ESTADO_MENU;
    }
}

void atualizarJogo(Sessao *s, Botoes *botoes, Recursos *recursos, PainelDevConfig *devCfg, Vector2 mouse, bool clique, float dt)
{
    // ---------- MODO DEV ----------

    if (IsKeyPressed(KEY_F1))
        s->modoDev = !s->modoDev;

    // dtSimulado é o "dt de jogo": igual ao dt real, exceto que o modo dev
    // pode multiplicá-lo pra acelerar o tempo. epochCriacao é deslocado pelo
    // tempo "extra" pra a hora-do-dia do pet acompanhar.
    float dtSimulado = dt;

    if (s->modoDev)
    {
        dtSimulado = dt * devCfg->velocidades[s->velocidadeDevIndice];
        s->pet.epochCriacao -= (long long)(dtSimulado - dt);
    }

    // ---------- ANIMAÇÃO ----------

    s->tempoAnimacao += dt;

    if (s->tempoAnimacao >= 0.5f)
    {
        s->tempoAnimacao = 0.0f;
        s->framePet = (s->framePet == 0) ? 1 : 0;
    }

    // ---------- NECESSIDADES + AMOSTRAGEM PARA EVOLUÇÃO ----------

    s->relogios.tick += dtSimulado;

    if (s->relogios.tick >= INTERVALO_TICK)
    {
        s->relogios.tick = 0.0f;
        executarTick(&s->pet, s->historico, &s->numHistorico);
    }

    // ---------- ENVELHECIMENTO / NÍVEL / EVOLUÇÃO ----------

    s->relogios.idade += dtSimulado;

    if (s->relogios.idade >= INTERVALO_DIA && s->pet.vivo)
    {
        s->relogios.idade = 0.0f;

        char mensagemEvolucao[100];
        bool tornouAdulto = executarAvancoDeDia(&s->pet, s->historico, &s->numHistorico, mensagemEvolucao, sizeof(mensagemEvolucao));

        if (tornouAdulto)
        {
            snprintf(s->popupAdultoTexto, sizeof(s->popupAdultoTexto), "Parabens! %s agora e um adulto!", s->pet.nome);
            s->estado = ESTADO_ADULTO_POPUP;
        }
        else if (mensagemEvolucao[0] != '\0')
        {
            strncpy(s->bannerEvolucaoTexto, mensagemEvolucao, sizeof(s->bannerEvolucaoTexto) - 1);
            s->bannerEvolucaoTexto[sizeof(s->bannerEvolucaoTexto) - 1] = '\0';
            s->bannerEvolucaoTimer = 5.0f;
        }
    }

    // ---------- LUZ ----------
    // (a recuperação de energia e o acordar automático agora ficam dentro de
    // executarTick, junto com o resto das necessidades — assim funcionam
    // também durante o tempo offline)

    if (s->pet.dormindo && s->luzAcesa)
    {
        s->relogios.acumuladorLuz += dtSimulado;

        if (s->relogios.acumuladorLuz >= 1800.0f) // dormir de luz acesa incomoda, a cada 30 min
        {
            s->relogios.acumuladorLuz = 0.0f;
            s->pet.felicidade -= 2;
            limitarStatus(&s->pet);
        }
    }

    // ---------- COCÔ ----------

    s->relogios.coco += dtSimulado;

    if (s->relogios.coco >= s->relogios.proximoCoco)
    {
        s->relogios.coco = 0.0f;
        s->relogios.proximoCoco = (float)GetRandomValue((int)COCO_INTERVALO_MIN, (int)COCO_INTERVALO_MAX);
        adicionarCoco(&s->pet);
    }

    // ---------- CHAMADO DE ATENÇÃO ----------

    s->relogios.atencao += dtSimulado;

    if (s->relogios.atencao >= s->relogios.proximaAtencao && !s->pet.pedindoAtencao && !s->pet.dormindo)
    {
        s->relogios.atencao = 0.0f;
        s->relogios.proximaAtencao = calcularProximaAtencao(s->pet.disciplina);

        s->pet.pedindoAtencao = true;
        s->pet.atencaoPorNecessidade = precisaCuidado(&s->pet); // fixado agora, não recalculado depois

        s->relogios.choro = 0.0f;
        PlaySound(recursos->sonsChoro[indiceSomChoro(s->pet.estagio)]);
    }

    // Enquanto estiver pedindo atenção, repete o choro a cada 3s reais (não
    // usa dtSimulado — o modo dev não deve acelerar o som).
    if (s->pet.pedindoAtencao)
    {
        s->relogios.choro += dt;

        if (s->relogios.choro >= 3.0f)
        {
            s->relogios.choro -= 3.0f;
            PlaySound(recursos->sonsChoro[indiceSomChoro(s->pet.estagio)]);
        }
    }
    else
    {
        s->relogios.choro = 0.0f;
    }

    // ---------- AUTOSAVE ----------

    s->relogios.autosave += dt;

    if (s->relogios.autosave >= INTERVALO_AUTOSAVE)
    {
        s->relogios.autosave = 0.0f;
        salvarJogo(&s->pet, s->historico, s->numHistorico);
    }

    if (s->mensagemSalvoTimer > 0.0f)
        s->mensagemSalvoTimer -= dt;

    if (s->mensagemAcaoTimer > 0.0f)
        s->mensagemAcaoTimer -= dt;

    if (s->cooldowns.refeicao > 0.0f) s->cooldowns.refeicao -= dtSimulado;
    if (s->cooldowns.petisco  > 0.0f) s->cooldowns.petisco  -= dtSimulado;
    if (s->cooldowns.brincar  > 0.0f) s->cooldowns.brincar  -= dtSimulado;
    if (s->cooldowns.remedio  > 0.0f) s->cooldowns.remedio  -= dtSimulado;

    if (s->bannerEvolucaoTimer > 0.0f)
        s->bannerEvolucaoTimer -= dt;

    // ---------- MORTE ----------
    // (o arquivamento em "pets descobertos" já é feito dentro de executarTick)

    if (!s->pet.vivo)
    {
        salvarJogo(&s->pet, s->historico, s->numHistorico);
        s->estado = ESTADO_MORTE;
    }

    // ---------- CLIQUES ----------

    if (!clique) return;

    if (CheckCollisionPointRec(mouse, botoes->jogo.refeicao) && s->cooldowns.refeicao <= 0.0f)
    {
        if (darRefeicao(&s->pet))
            PlaySound(recursos->somComer);
        else
        {
            snprintf(s->mensagemAcaoTexto, sizeof(s->mensagemAcaoTexto), "%s recusou a refeicao!", s->pet.nome);
            s->mensagemAcaoTimer = 2.0f;
            s->cooldowns.refeicao = COOLDOWN_RECUSA_SEGUNDOS;
        }
    }

    if (CheckCollisionPointRec(mouse, botoes->jogo.petisco) && s->cooldowns.petisco <= 0.0f)
    {
        if (darPetisco(&s->pet))
            PlaySound(recursos->somComer);
        else
        {
            snprintf(s->mensagemAcaoTexto, sizeof(s->mensagemAcaoTexto), "%s recusou o petisco!", s->pet.nome);
            s->mensagemAcaoTimer = 2.0f;
            s->cooldowns.petisco = COOLDOWN_RECUSA_SEGUNDOS;
        }
    }

    if (CheckCollisionPointRec(mouse, botoes->jogo.brincar) && s->cooldowns.brincar <= 0.0f)
    {
        if (brincar(&s->pet))
            PlaySound(recursos->somBrincar);
        else
        {
            snprintf(s->mensagemAcaoTexto, sizeof(s->mensagemAcaoTexto), "%s nao quis brincar agora!", s->pet.nome);
            s->mensagemAcaoTimer = 2.0f;
            s->cooldowns.brincar = COOLDOWN_RECUSA_SEGUNDOS;
        }
    }

    if (CheckCollisionPointRec(mouse, botoes->jogo.dormir))
        alternarDormir(&s->pet);

    if (CheckCollisionPointRec(mouse, botoes->jogo.remedio) && s->cooldowns.remedio <= 0.0f && !darRemedio(&s->pet) && s->pet.doente)
    {
        snprintf(s->mensagemAcaoTexto, sizeof(s->mensagemAcaoTexto), "%s recusou o remedio!", s->pet.nome);
        s->mensagemAcaoTimer = 2.0f;
        s->cooldowns.remedio = COOLDOWN_RECUSA_SEGUNDOS;
    }

    if (CheckCollisionPointRec(mouse, botoes->jogo.elogiar))
        elogiar(&s->pet);

    if (CheckCollisionPointRec(mouse, botoes->jogo.repreender))
        repreender(&s->pet);

    if (CheckCollisionPointRec(mouse, botoes->jogo.luz))
        s->luzAcesa = !s->luzAcesa;

    if (CheckCollisionPointRec(mouse, botoes->jogo.salvar))
    {
        salvarJogo(&s->pet, s->historico, s->numHistorico);
        s->saveExiste = true;
        s->mensagemSalvoTimer = 2.0f;
    }

    if (CheckCollisionPointRec(mouse, botoes->jogo.menuTopo))
    {
        salvarJogo(&s->pet, s->historico, s->numHistorico);
        s->saveExiste = true;
        s->estado = ESTADO_MENU;
    }

    if (s->modoDev)
    {
        if (CheckCollisionPointRec(mouse, botoes->dev.velMenos))
            s->velocidadeDevIndice = (s->velocidadeDevIndice + devCfg->numVelocidades - 1) % devCfg->numVelocidades;

        if (CheckCollisionPointRec(mouse, botoes->dev.velMais))
            s->velocidadeDevIndice = (s->velocidadeDevIndice + 1) % devCfg->numVelocidades;

        if (CheckCollisionPointRec(mouse, botoes->dev.doente))
            s->pet.doente = !s->pet.doente;

        if (CheckCollisionPointRec(mouse, botoes->dev.avancarDia))
        {
            char mensagemDevDescartavel[100];
            bool tornouAdultoDev = executarAvancoDeDia(&s->pet, s->historico, &s->numHistorico, mensagemDevDescartavel, sizeof(mensagemDevDescartavel));

            if (tornouAdultoDev)
            {
                snprintf(s->popupAdultoTexto, sizeof(s->popupAdultoTexto), "Parabens! %s agora e um adulto!", s->pet.nome);
                s->estado = ESTADO_ADULTO_POPUP;
            }
        }

        if (CheckCollisionPointRec(mouse, botoes->dev.horaMenos))
            s->pet.epochCriacao += 3600;

        if (CheckCollisionPointRec(mouse, botoes->dev.horaMais))
            s->pet.epochCriacao -= 3600;
    }

    // Limpar cocô clicando diretamente nele — só com a luz acesa (com a luz
    // apagada o cocô fica só invisível, não sumiu de verdade, então não pode
    // ser limpo sem querer no escuro)
    if (s->luzAcesa)
    {
        for (int i = 0; i < s->pet.numCocos; i++)
        {
            float distX = mouse.x - s->pet.cocos[i].x;
            float distY = mouse.y - s->pet.cocos[i].y;
            float distancia = (distX * distX) + (distY * distY);

            if (distancia <= (18 * 18))
            {
                limparCoco(&s->pet, i);
                break;
            }
        }
    }
}

void atualizarAdultoPopup(Sessao *s, Botoes *botoes, Vector2 mouse, bool clique)
{
    if (!clique) return;

    if (CheckCollisionPointRec(mouse, botoes->popupAdulto.novoPet))
    {
        // O pet já foi arquivado em "pets descobertos" no momento em que virou
        // adulto; aqui só salvamos e seguimos para criar o próximo pet.
        salvarJogo(&s->pet, s->historico, s->numHistorico);

        s->nomeDigitado[0] = '\0';
        s->letraCount = 0;
        s->horaEscolhida = obterHoraLocalAtual();
        s->estado = ESTADO_NOMEAR;
    }

    if (CheckCollisionPointRec(mouse, botoes->popupAdulto.continuar))
    {
        s->estado = ESTADO_JOGO;
    }
}

void atualizarMorte(Sessao *s, Botoes *botoes, Vector2 mouse, bool clique)
{
    if (!clique) return;

    if (CheckCollisionPointRec(mouse, botoes->morte.novoJogo))
    {
        s->nomeDigitado[0] = '\0';
        s->letraCount = 0;
        s->horaEscolhida = obterHoraLocalAtual();
        s->estado = ESTADO_NOMEAR;
    }

    if (CheckCollisionPointRec(mouse, botoes->morte.sair))
    {
        s->estado = ESTADO_MENU;
    }
}


// ==================================================
// TELAS — DESENHAR
// ==================================================

void desenharTelaMenu(Sessao *s, Botoes *botoes, int largura)
{
    const char *titulo = "MEU PET VIRTUAL";
    int tamTitulo = 44;
    int largTitulo = MeasureText(titulo, tamTitulo);

    DrawText(titulo, largura / 2 - largTitulo / 2, 180, tamTitulo, DARKGRAY);

    desenharBotao("NOVO JOGO", botoes->menu.novoJogo);

    if (s->saveExiste)
        desenharBotao("CARREGAR JOGO", botoes->menu.carregarJogo);
    else
        desenharBotaoDesabilitado("CARREGAR JOGO", botoes->menu.carregarJogo);

    desenharBotao("PETS DESCOBERTOS", botoes->menu.verHistorico);

    if (!s->saveExiste)
    {
        const char *aviso = "Nenhum save encontrado";
        int largAviso = MeasureText(aviso, 18);
        DrawText(aviso, largura / 2 - largAviso / 2, 550, 18, GRAY);
    }
}

void desenharTelaHistorico(Sessao *s, Botoes *botoes, Recursos *recursos, int largura)
{
    desenharHistorico(s->historico, s->numHistorico, s->paginaHistorico, recursos->texturasEstagio, largura);

    const int itensPorPagina = 6;
    int totalPaginas = (s->numHistorico + itensPorPagina - 1) / itensPorPagina;
    if (totalPaginas < 1) totalPaginas = 1;

    if (s->paginaHistorico > 0)
        desenharBotao("ANTERIOR", botoes->historico.anterior);
    else
        desenharBotaoDesabilitado("ANTERIOR", botoes->historico.anterior);

    if (s->paginaHistorico < totalPaginas - 1)
        desenharBotao("PROXIMA", botoes->historico.proxima);
    else
        desenharBotaoDesabilitado("PROXIMA", botoes->historico.proxima);

    desenharBotao("VOLTAR", botoes->historico.voltar);
}

void desenharTelaNomear(Sessao *s, Botoes *botoes, int largura)
{
    const char *titulo = "COMO SE CHAMA SEU PET?";
    int tamTitulo = 30;
    int largTitulo = MeasureText(titulo, tamTitulo);

    DrawText(titulo, largura / 2 - largTitulo / 2, 250, tamTitulo, DARKGRAY);

    DrawRectangleRec(botoes->nomear.caixaNome, RAYWHITE);
    DrawRectangleLinesEx(botoes->nomear.caixaNome, 2, DARKGRAY);

    bool cursorAceso = (((int)(GetTime() * 2.0f)) % 2) == 0;

    const char *textoExibido = TextFormat(
        "%s%s",
        s->nomeDigitado,
        cursorAceso ? "_" : ""
    );

    DrawText(textoExibido, botoes->nomear.caixaNome.x + 10, botoes->nomear.caixaNome.y + 13, 24, DARKGRAY);

    const char *perguntaHora = "Que horas sao agora?";
    int largPerguntaHora = MeasureText(perguntaHora, 18);
    DrawText(perguntaHora, largura / 2 - largPerguntaHora / 2, 350, 18, DARKGRAY);

    desenharBotao("-", botoes->nomear.horaMenos);
    desenharBotao("+", botoes->nomear.horaMais);

    const char *textoHora = TextFormat("%02dh", s->horaEscolhida);
    int largTextoHora = MeasureText(textoHora, 26);
    DrawText(textoHora, largura / 2 - largTextoHora / 2, 383, 26, DARKGRAY);

    bool podeComecar = (s->letraCount > 0);

    if (podeComecar)
        desenharBotao("COMECAR", botoes->nomear.comecar);
    else
        desenharBotaoDesabilitado("COMECAR", botoes->nomear.comecar);

    desenharBotao("VOLTAR", botoes->nomear.voltar);
}

// Desenha a tela de jogo e, se for o caso, o popup modal de "virou adulto"
// por cima dela (o popup pausa a tela de jogo por baixo, então os dois
// dividem esta mesma função de desenho).
void desenharTelaJogo(
    Sessao *s,
    Botoes *botoes,
    Recursos *recursos,
    PainelDevConfig *devCfg,
    int largura,
    int altura,
    int centroPetX,
    int centroPetY
)
{
    DrawText("Meu Pet Virtual", 50, 40, 40, DARKGRAY);
    DrawText(TextFormat("Nome: %s", s->pet.nome), 50, 85, 20, DARKGRAY);

    // Com a luz apagada não dá para ver o painel de status nem o cocô no chão
    if (s->luzAcesa)
    {
        desenharStatus(&s->pet);
        desenharCocos(&s->pet);
    }

    desenharPet(
        &s->pet, recursos->texturasEstagio,
        recursos->bebeIdle1, recursos->bebeIdle2,
        recursos->juvenil1Idle1, recursos->juvenil1Idle2,
        recursos->juvenil2Idle1, recursos->juvenil2Idle2,
        s->framePet, centroPetX, centroPetY
    );

    desenharDoenca(&s->pet, centroPetX + 90, centroPetY - 120);
    desenharAtencao(&s->pet, centroPetX - 90, centroPetY - 120);

    desenharBotoesDeAcao(&s->pet, s->cooldowns, s->luzAcesa, botoes->jogo);

    // Posições logo abaixo da coluna de botões de ação (860, 120+55*9+10/+35)
    if (s->mensagemSalvoTimer > 0.0f)
        DrawText("Jogo salvo!", 860, 625, 20, DARKGREEN);

    if (s->mensagemAcaoTimer > 0.0f)
        DrawText(s->mensagemAcaoTexto, 860, 650, 18, MAROON);

    if (s->bannerEvolucaoTimer > 0.0f)
    {
        int largBanner = MeasureText(s->bannerEvolucaoTexto, 26);

        DrawRectangle(largura / 2 - largBanner / 2 - 20, 90, largBanner + 40, 45, Fade(GOLD, 0.9f));
        DrawRectangleLines(largura / 2 - largBanner / 2 - 20, 90, largBanner + 40, 45, DARKBROWN);
        DrawText(s->bannerEvolucaoTexto, largura / 2 - largBanner / 2, 100, 26, DARKBROWN);
    }

    // Escurece a tela quando a luz está apagada
    if (!s->luzAcesa)
    {
        float alpha = s->pet.dormindo ? 0.75f : 0.55f;
        DrawRectangle(0, 0, largura, altura, Fade(BLACK, alpha));

        // Redesenha os botões por cima do escurecimento para continuarem visíveis/clicáveis
        desenharBotoesDeAcao(&s->pet, s->cooldowns, s->luzAcesa, botoes->jogo);
    }

    // Popup modal de evolução para adulto (bloqueia a tela de jogo por baixo)
    if (s->estado == ESTADO_ADULTO_POPUP)
    {
        DrawRectangle(0, 0, largura, altura, Fade(BLACK, 0.6f));

        int tamMsg = 22;
        int largMsg = MeasureText(s->popupAdultoTexto, tamMsg);

        int largCaixa = largMsg + 80;
        if (largCaixa < 600) largCaixa = 600;

        int xCaixa = largura / 2 - largCaixa / 2;
        int yCaixa = 240;
        int altCaixa = 210;

        DrawRectangle(xCaixa, yCaixa, largCaixa, altCaixa, Fade(GOLD, 0.95f));
        DrawRectangleLines(xCaixa, yCaixa, largCaixa, altCaixa, DARKBROWN);

        DrawText(s->popupAdultoTexto, largura / 2 - largMsg / 2, yCaixa + 30, tamMsg, DARKBROWN);

        desenharBotao("1 - CRIAR NOVO PET", botoes->popupAdulto.novoPet);
        desenharBotao("2 - CONTINUAR", botoes->popupAdulto.continuar);
    }

    // ---------- PAINEL DO MODO DEV ----------

    if (s->modoDev)
    {
        DrawRectangleRec(botoes->dev.painel, BLACK);
        DrawRectangleLinesEx(botoes->dev.painel, 2, RED);

        DrawText("MODO DEV (F1 para sair)", botoes->dev.painel.x + 15, botoes->dev.painel.y + 10, 22, RED);

        DrawText(
            TextFormat("Velocidade: %.0fx", devCfg->velocidades[s->velocidadeDevIndice]),
            botoes->dev.painel.x + 15, botoes->dev.painel.y + 48, 18, WHITE
        );
        desenharBotao("-", botoes->dev.velMenos);
        desenharBotao("+", botoes->dev.velMais);

        for (int i = 0; i < 7; i++)
        {
            controleDevStat(
                devCfg->nomes[i], devCfg->ponteiros[i],
                devCfg->minimo[i], devCfg->maximo[i], devCfg->passo[i],
                botoes->dev.statMenos[i], botoes->dev.statMais[i],
                botoes->dev.painel.x + 15, botoes->dev.painel.y + 98 + i * 42
            );
        }

        desenharBotao(s->pet.doente ? "CURAR" : "ADOECER", botoes->dev.doente);
        desenharBotao("AVANCAR 1 DIA", botoes->dev.avancarDia);
        desenharBotao("HORA -1h", botoes->dev.horaMenos);
        desenharBotao("HORA +1h", botoes->dev.horaMais);

        float horaDev = horaAtualDoPet(&s->pet);

        DrawText(
            TextFormat(
                "Hora do pet: %02d:%02dh (%s)%s",
                (int)horaDev,
                (int)((horaDev - (int)horaDev) * 60.0f),
                ehPeriodoNoturno(horaDev) ? "NOITE" : "DIA",
                s->pet.sonoProfundo ? " - sono profundo" : ""
            ),
            botoes->dev.painel.x + 15, botoes->dev.painel.y + 530, 18, YELLOW
        );
    }
}

void desenharTelaMorte(Sessao *s, Botoes *botoes, int largura)
{
    const char *titulo = TextFormat("%s nao resistiu...", s->pet.nome);
    int tamTitulo = 34;
    int largTitulo = MeasureText(titulo, tamTitulo);

    DrawText(titulo, largura / 2 - largTitulo / 2, 220, tamTitulo, MAROON);

    const char *subtitulo = TextFormat(
        "Viveu %d dia(s), nivel %d, estagio %s",
        s->pet.idade,
        s->pet.nivel,
        nomeDoEstagio(s->pet.estagio)
    );

    int largSub = MeasureText(subtitulo, 20);
    DrawText(subtitulo, largura / 2 - largSub / 2, 270, 20, DARKGRAY);

    const char *dica = "Negligencia (fome, sujeira ou doenca) pode ser fatal.";
    int largDica = MeasureText(dica, 18);
    DrawText(dica, largura / 2 - largDica / 2, 320, 18, GRAY);

    desenharBotao("NOVO JOGO", botoes->morte.novoJogo);
    desenharBotao("MENU", botoes->morte.sair);
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

    Sessao sessao = {0};

    sessao.estado = ESTADO_MENU;
    sessao.luzAcesa = true;
    sessao.horaEscolhida = obterHoraLocalAtual(); // "que horas são?" na criação do pet
    sessao.relogios.proximoCoco = (float)GetRandomValue((int)COCO_INTERVALO_MIN, (int)COCO_INTERVALO_MAX);
    sessao.relogios.proximaAtencao = (float)GetRandomValue(20, 35);


    // ==================================================
    // CRIAR JANELA
    // ==================================================

    InitWindow(largura, altura, "Meu Pet Virtual");
    InitAudioDevice();

    SetTargetFPS(60);

    sessao.saveExiste = arquivoDeSaveExiste();

    // Carrega o histórico (e o pet salvo) desde já, para que "PETS DESCOBERTOS"
    // funcione no menu mesmo antes de o jogador clicar em "CARREGAR JOGO".
    if (sessao.saveExiste)
        carregarJogo(&sessao.pet, sessao.historico, &sessao.numHistorico);


    // ==================================================
    // CARREGAR RECURSOS (SPRITES / ÁUDIO)
    // ==================================================

    Recursos recursos = {0};

    recursos.texturasEstagio[ESTAGIO_ADULTO1A] = carregarTexturaDoAsset("adulto1a.png");
    recursos.texturasEstagio[ESTAGIO_ADULTO2A] = carregarTexturaDoAsset("adulto2a.png");
    recursos.texturasEstagio[ESTAGIO_ADULTO3A] = carregarTexturaDoAsset("adulto3a.png");
    recursos.texturasEstagio[ESTAGIO_ADULTO1B] = carregarTexturaDoAsset("adulto1b.png");
    recursos.texturasEstagio[ESTAGIO_ADULTO2B] = carregarTexturaDoAsset("adulto2b.png");
    recursos.texturasEstagio[ESTAGIO_ADULTO3B] = carregarTexturaDoAsset("adulto3b.png");

    // Bebe, Juvenil1 e Juvenil2 tem sprites de animacao (idle1 / idle2)
    recursos.bebeIdle1 = carregarTexturaDoAsset("bebe_idle1.png");
    recursos.bebeIdle2 = carregarTexturaDoAsset("bebe_idle2.png");
    recursos.juvenil1Idle1 = carregarTexturaDoAsset("juvenil1_idle1.png");
    recursos.juvenil1Idle2 = carregarTexturaDoAsset("juvenil1_idle2.png");
    recursos.juvenil2Idle1 = carregarTexturaDoAsset("juvenil2_idle1.png");
    recursos.juvenil2Idle2 = carregarTexturaDoAsset("juvenil2_idle2.png");

    // sonsChoro[indiceSomChoro(estagio)] — adulto1a/1b compartilham som, idem 2a/2b e 3a/3b
    recursos.sonsChoro[0] = carregarSomDoAsset("baby_cry.ogg");
    recursos.sonsChoro[1] = carregarSomDoAsset("juvenil1_cry.ogg");
    recursos.sonsChoro[2] = carregarSomDoAsset("juvenil2_cry.ogg");
    recursos.sonsChoro[3] = carregarSomDoAsset("adulto1ab_cry.wav");
    recursos.sonsChoro[4] = carregarSomDoAsset("adulto2ab_cry.wav");
    recursos.sonsChoro[5] = carregarSomDoAsset("adulto3ab_cry.wav");

    recursos.somComer = carregarSomDoAsset("eating.mp3");
    recursos.somBrincar = carregarSomDoAsset("playing.mp3");

    recursos.musicaFundo = carregarMusicaDoAsset("background_music.mp3");
    SetMusicVolume(recursos.musicaFundo, 0.15f); // bem baixinho, é só ambiente
    PlayMusicStream(recursos.musicaFundo);


    // ==================================================
    // POSIÇÃO DO PET NA TELA
    // ==================================================

    int centroPetX = 500;
    int centroPetY = 420;


    // ==================================================
    // MODO DEV (F1) — acelerar o tempo e manipular status pra testar
    // ==================================================

    PainelDevConfig devCfg = {
        .ponteiros = {
            &sessao.pet.fome, &sessao.pet.felicidade, &sessao.pet.energia,
            &sessao.pet.saude, &sessao.pet.disciplina, &sessao.pet.peso, &sessao.pet.nivel
        },
        .nomes = { "Fome", "Felicidade", "Energia", "Saude", "Disciplina", "Peso", "Nivel" },
        .minimo = { 0, 0, 0, 0, 0, 10, 1 },
        .maximo = { 100, 100, 100, 100, 100, 200, 999 },
        .passo = { 10, 10, 10, 10, 10, 10, 1 },
        .velocidades = { 1.0f, 10.0f, 60.0f, 300.0f, 1800.0f },
        .numVelocidades = 5
    };


    // ==================================================
    // BOTÕES DE TODAS AS TELAS
    // ==================================================

    Botoes botoes = {
        .menu = {
            .novoJogo     = { largura / 2 - 140, 330, 280, 55 },
            .carregarJogo = { largura / 2 - 140, 400, 280, 55 },
            .verHistorico = { largura / 2 - 140, 470, 280, 55 },
        },
        .popupAdulto = {
            .novoPet   = { largura / 2 - 290, 360, 270, 55 },
            .continuar = { largura / 2 + 20,  360, 270, 55 },
        },
        .historico = {
            .anterior = { largura / 2 - 300, 680, 140, 45 },
            .proxima  = { largura / 2 + 160, 680, 140, 45 },
            .voltar   = { largura / 2 - 70,  680, 140, 45 },
        },
        .nomear = {
            .caixaNome = { largura / 2 - 200, 300, 400, 50 },
            .horaMenos = { largura / 2 - 90,  375, 45,  40 },
            .horaMais  = { largura / 2 + 45,  375, 45,  40 },
            .comecar   = { largura / 2 - 140, 440, 280, 55 },
            .voltar    = { largura / 2 - 140, 510, 280, 55 },
        },
        .jogo = {
            .refeicao   = { 860, 120 + 55 * 0, 200, 45 },
            .petisco    = { 860, 120 + 55 * 1, 200, 45 },
            .brincar    = { 860, 120 + 55 * 2, 200, 45 },
            .dormir     = { 860, 120 + 55 * 3, 200, 45 },
            .remedio    = { 860, 120 + 55 * 4, 200, 45 },
            .elogiar    = { 860, 120 + 55 * 5, 200, 45 },
            .repreender = { 860, 120 + 55 * 6, 200, 45 },
            .luz        = { 860, 120 + 55 * 7, 200, 45 },
            .salvar     = { 860, 120 + 55 * 8, 200, 45 },
            .menuTopo   = { largura - 150, 30, 110, 35 },
        },
        .dev = {
            .painel = { 40, 95, 680, 615 },
        },
        .morte = {
            .novoJogo = { largura / 2 - 140, 420, 280, 55 },
            .sair     = { largura / 2 - 140, 490, 280, 55 },
        },
    };

    // Botões do painel dev dependem da posição do painel, calculados à parte
    botoes.dev.velMenos = (Rectangle){ botoes.dev.painel.x + 380, botoes.dev.painel.y + 40, 40, 32 };
    botoes.dev.velMais  = (Rectangle){ botoes.dev.painel.x + 430, botoes.dev.painel.y + 40, 40, 32 };

    for (int i = 0; i < 7; i++)
    {
        int yLinha = botoes.dev.painel.y + 90 + i * 42;
        botoes.dev.statMenos[i] = (Rectangle){ botoes.dev.painel.x + 380, yLinha, 40, 32 };
        botoes.dev.statMais[i]  = (Rectangle){ botoes.dev.painel.x + 430, yLinha, 40, 32 };
    }

    botoes.dev.doente     = (Rectangle){ botoes.dev.painel.x + 15,  botoes.dev.painel.y + 480, 150, 36 };
    botoes.dev.avancarDia = (Rectangle){ botoes.dev.painel.x + 180, botoes.dev.painel.y + 480, 170, 36 };
    botoes.dev.horaMenos  = (Rectangle){ botoes.dev.painel.x + 365, botoes.dev.painel.y + 480, 80,  36 };
    botoes.dev.horaMais   = (Rectangle){ botoes.dev.painel.x + 455, botoes.dev.painel.y + 480, 80,  36 };


    // ==================================================
    // GAME LOOP
    // ==================================================

    while (!WindowShouldClose())
    {
        float dt = GetFrameTime();

        UpdateMusicStream(recursos.musicaFundo); // precisa rodar sempre, em qualquer tela

        Vector2 mouse = GetMousePosition();
        bool clique = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);

        switch (sessao.estado)
        {
            case ESTADO_MENU:         atualizarMenu(&sessao, &botoes, mouse, clique); break;
            case ESTADO_HISTORICO:    atualizarHistorico(&sessao, &botoes, mouse, clique); break;
            case ESTADO_NOMEAR:       atualizarNomear(&sessao, &botoes, mouse, clique); break;
            case ESTADO_JOGO:         atualizarJogo(&sessao, &botoes, &recursos, &devCfg, mouse, clique, dt); break;
            case ESTADO_ADULTO_POPUP: atualizarAdultoPopup(&sessao, &botoes, mouse, clique); break;
            case ESTADO_MORTE:        atualizarMorte(&sessao, &botoes, mouse, clique); break;
        }

        BeginDrawing();

        ClearBackground(RAYWHITE);

        switch (sessao.estado)
        {
            case ESTADO_MENU:      desenharTelaMenu(&sessao, &botoes, largura); break;
            case ESTADO_HISTORICO: desenharTelaHistorico(&sessao, &botoes, &recursos, largura); break;
            case ESTADO_NOMEAR:    desenharTelaNomear(&sessao, &botoes, largura); break;

            // O popup de "virou adulto" é desenhado como parte da tela de
            // jogo (pausada por baixo dele) — ver desenharTelaJogo.
            case ESTADO_JOGO:
            case ESTADO_ADULTO_POPUP:
                desenharTelaJogo(&sessao, &botoes, &recursos, &devCfg, largura, altura, centroPetX, centroPetY);
                break;

            case ESTADO_MORTE: desenharTelaMorte(&sessao, &botoes, largura); break;
        }

        EndDrawing();
    }


    // ==================================================
    // SALVAR ANTES DE SAIR
    // ==================================================

    if (sessao.estado == ESTADO_JOGO)
        salvarJogo(&sessao.pet, sessao.historico, sessao.numHistorico);


    // ==================================================
    // LIBERAR MEMÓRIA
    // ==================================================

    for (int i = 0; i < ESTAGIO_TOTAL; i++)
    {
        if (i == ESTAGIO_BEBE || i == ESTAGIO_JUVENIL1 || i == ESTAGIO_JUVENIL2)
            continue; // essas nao usam texturasEstagio, tem suas proprias texturas de animacao

        UnloadTexture(recursos.texturasEstagio[i]);
    }

    UnloadTexture(recursos.bebeIdle1);
    UnloadTexture(recursos.bebeIdle2);
    UnloadTexture(recursos.juvenil1Idle1);
    UnloadTexture(recursos.juvenil1Idle2);
    UnloadTexture(recursos.juvenil2Idle1);
    UnloadTexture(recursos.juvenil2Idle2);

    for (int i = 0; i < 6; i++)
        UnloadSound(recursos.sonsChoro[i]);

    UnloadSound(recursos.somComer);
    UnloadSound(recursos.somBrincar);
    UnloadMusicStream(recursos.musicaFundo);

    CloseAudioDevice();
    CloseWindow();

    return 0;
}
