
Meu primeiro jogo: um simulador de bichinho virtual (tipo Tamagotchi), feito em C com [raylib](https://www.raylib.com/).

Cuide do seu pet, mantenha os status em dia e acompanhe ele evoluir de bebê a adulto — inclusive enquanto o jogo está fechado.

<img width="1097" height="765" alt="Screenshot 2026-08-06 181328" src="https://github.com/user-attachments/assets/7aa0f192-91ba-4754-88e5-a83bd3abf2a9" />


## Como jogar

1. Abra o jogo (`mypetgame/output/main.exe` no Windows, ou compile a partir do código-fonte — veja abaixo).
2. No menu, clique em **NOVO JOGO**, dê um nome ao seu pet e informe que horas são no momento (o jogo usa isso pra simular um ciclo de dia/noite realista).
3. Cuide do pet clicando nos botões de ação. Todo o jogo é jogado com o mouse.
4. Se já existir um save, use **CARREGAR JOGO** para continuar de onde parou. O tempo que você ficou offline é simulado automaticamente ao voltar.

## Status do pet

| Status | O que representa |
|---|---|
| **Fome** | Cai com o tempo. Se chegar a 0, a saúde começa a cair. |
| **Felicidade** | Cai com o tempo e quando o pet fica pedindo atenção sem resposta. |
| **Energia** | Cai enquanto o pet está acordado (mais rápido de madrugada). Recupera dormindo. |
| **Saúde** | Cai por negligência (fome zerada, sujeira acumulada, doença). Se chegar a 0, o pet morre. |
| **Disciplina** | Sobe ou desce conforme você elogia/repreende. Tende a voltar sozinha para o valor neutro (50) com o tempo. |
| **Peso** | Sobe ao comer, desce ao brincar. Acima de 80 o pet fica "sobrepeso" e piora mais quando doente. |

## Ações disponíveis

- **Refeição** — mata bastante fome, engorda um pouco.
- **Petisco** — mata pouca fome, mas dá bastante felicidade (engorda o dobro da refeição).
- **Brincar** — aumenta bastante a felicidade e reduz a fome, mas gasta energia (precisa de pelo menos 20).
- **Dormir** — alterna entre acordado e dormindo. Dormir à noite (18h–6h) é "sono profundo": recupera mais energia, a fome quase não cai, e o pet só acorda sozinho às 6h (você pode acordá-lo antes, mas ele fica triste e mais cansado).
- **Remédio** — só disponível quando o pet está doente; cura a doença mas o pet reclama do gosto (perde um pouco de felicidade).
- **Elogiar** — sobe felicidade, mas mal-acostuma o pet (baixa disciplina).
- **Repreender** — sobe disciplina e baixa felicidade. Repreender demais em sequência sem nenhum carinho no meio perde efeito e passa a piorar a disciplina.
- **Limpar cocô** — clique nos cocôs que aparecem na tela; deixar acumular prejudica a saúde.

Dependendo da disciplina do pet, ele pode simplesmente **recusar** refeição/petisco/brincar/remédio — quanto menor a disciplina, maior a chance de recusa. Um botão recusado fica bloqueado por alguns minutos antes de poder ser usado de novo.

## Pedidos de atenção

De tempos em tempos (mais seguido com disciplina baixa) o pet pede atenção. Se fome ou felicidade estiverem baixas, alimentar ou brincar resolve o pedido; se ambas já estiverem altas, é só carência — elogiar ou repreender resolve.

## Doença e morte

O pet pode ficar doente com mais chance quando a saúde, a fome ou a felicidade estão baixas, ou quando há sujeira acumulada. Doença não tratada continua corroendo a saúde. Se a saúde chegar a 0, o pet morre — e vai para a lista de **PETS DESCOBERTOS**, no menu principal.

## Evolução

<img width="1097" height="1097" alt="Picsart_26-08-07_12-47-14-242" src="https://github.com/user-attachments/assets/b8814fdd-f1d1-444c-99c2-d5973b0119f5" />


O pet evolui em três fases, avançando um nível a cada "dia" do jogo:

- **Nível 10** — Bebê evolui para Juvenil (1 ou 2, dependendo da média recente dos status).
- **Nível 30** — Juvenil evolui para um dos três Adultos (1, 2 ou 3), também de acordo com a média recente dos status.

Quanto melhor o cuidado nas últimas horas antes de cada evolução, melhor o resultado — o jogo usa uma média que dá mais peso pra como o pet foi tratado recentemente, não desde o nascimento.

## Salvando o progresso

Use o botão **SALVAR** a qualquer momento (o jogo também salva automaticamente a cada minuto). O save fica em `output/save.dat`, em formato texto simples.

## Compilando do zero

O projeto usa [raylib](https://www.raylib.com/) (headers em `mypetgame/include`, lib estática em `mypetgame/lib`). Exemplo de compilação com MinGW/gcc a partir da pasta `mypetgame`:

```
gcc main.c -o output/main.exe -I include -L lib -lraylib -lopengl32 -lgdi32 -lwinmm
```
