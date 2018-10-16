# mmaplloc
malloc &amp; free &amp; realloc &amp; associated functions allowing backing overlay to filesystem mmapped files PLUS associated tools to help building numerous stores for data structure containers


1) a estrutura de cabeçalho do bloco livre (FreeHeader) pode ser:
	FAST_FREE   -- size_t total_size, size_t next_free, size_t prev_free -- 3*size_t
	TIGHT_MEM   -- size_t total_size, size_t next_free
	SUPER_TIGHT -- mesmo do TIGHT_MEM, porém permite alocações de 1*size_t
2) a estrutura de cabeçalho do bloco alocado (AllocHeader) pode ser:
	FAST_FREE ou TIGHT_MEM -- size_t total_size
	SUPER_TIGHT            -- nada ou size_t total_size
3) O mínimo espaço alocado terá tamanho total igual ao cabeçalho do bloco livre (2 ou 3 * size_t) -- exceto se o SUPER_TIGHT for usado, onde será possível alocar com, no mínimo, 1*size_t de tamanho total... que, neste caso, será o tamanho utilizável.
4) Alocações "normais" são consideradas quando sizeof(AllocHeader)+malloc_size são iguais ou maiores ao tamanho do head do bloco livre); alocações "especiais" são as que são menores que isso. Uma alocação especial ocupará o mesmo tamanho do cabeçalho do bloco livre se FAST_FREE ou TIGHT_MEM forem especificadas e ocupará o espaço de 1*size_t caso SUPER_TIGHT tenha sido especificado. Neste caso, 'free' jamais poderá ser chamado.
5) Se SUPER_TIGHT for usado, será possível executar uma sequencia de malloc(sizeof(size_t)) onde nenhum byte será disperdiçado com estruturas internas -- lembrando que, de acordo com (3), nenhuma dessas alocações poderá ser liberada com free.
6) (4) faz sentido?
7) é possível pedir a alocação de n página(s), onde o bloco será retornado alinhado com as páginas da máquina e não será gasto nenhum byte com overhead -- uma sequencia de palloc(n) retornará ponteiros contíguos. free, neste caso, inserirá os blocos em sua estrutura

OBS: podemos implementar o FAST_FREE e TIGHT_MEM automaticamente caso tenhamos um if (total_size >= sizeof(FAST_FREE_Header)) -- FreeHeader seria um union do FAST_FREE e TIGHT_MEM.

Módulo de páginas:
1) vector de inteiros terá o primeiro inteiro da primeira página reservado para armazenar o length dele. vector[0] aponta para base+sizeof(size_t) e os próximos (4096/sizeof(size_t))-2 elementos da primeira página estão disponíveis. O último elemento será o ponteiro (na verdade offset) para uma eventual próxima página. Pŕoximas páginas terão os primeiros (4096/sizeof(size_t))-1 úteis, sendo o último também reservado para uma eventual próxima página.
2) um vector de strings ou de tipos com tamanho variável (inclusive outros vectors) será representado como um vector de inteiros, onde cada elemento é um ponteiro (ou melhor, um offset), para o ítem real.
a)small_salloc(page, prevstr, , short_salloc, long_salloc are methods do allocate lists of strings inside a page -- essa é uma tarefa pra outro módulo
3) vectors de outros tamanhos (byte, short, long, float, double, struct), igualmente, terão o primeiro size_t da primeira página pra guardar o length e o último size_t para guardar o ponteiro offset para a próxima página

a) pode se usar malloc para alocar itens de tamanhos variáveis, porém haverá o overhead de um size_t, podendo chegar a 8 bytes. Uma outra alternativa seria criar listas contíguas. Por exemplo:
b) a unidade básica de alocação para elementos de tamanho variável consiste num número n de paginas sequenciais (alocadas com palloc(n)). O primeiro size_t da primeira página terá o número de páginas da sequencia e o último size_t da última página terá um ponteiro offset para uma eventual próxima sequência. Dos bytes úteis de cada sequencia, o primeiro inteiro size_t contém o offset para a primeira posição livre, ainda não alocada (em oposição a posições deletadas, que serão abordadas mais tarde), seja nesta sequencia de páginas ou em qualquer outra. Os bytes restantes conterão os elementos, dispostos da seguinte maneira: um inteiro (int8, int16, int32 ou int64) contém o comprimento do elemento, logo após, os bytes do elemento. O ponteiro para a proxima posição não alocada será atualizado... e assim por diante. Faltou ainda registrarmos que os bytes para o tamanho de cada elemento serão 8, 16, 32 ou 64 bits. Isso se dará de forma automática. É possível que o primeiro size_t da primeira página contenha o ponteiro offset para a primeira posição ainda não alocada e que o inteiro seguinte represente o numero de paginas da sequencia.
b) para strings ou arrays de bytes: cada página (?é, inicialmente, inicializada com zeros?) guarda, nos primeiros sizeof(size_t) bytes um ponteiro offset para a primeira sequencia de bytes não utilizada
