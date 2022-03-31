# 1C-Contest
**Требования:**
0. Предполагается использование программ на линуксе. Для использования нужно иметь компилятор(в примере показаны команды для компилятора gcc).
1. Каждый клиент должен подавать энтер хотя бы раз в 1000000 символов, иначе его отключают.
2. Если пытается подключиться шестой клиент, когда 5 уже подключено, то ему говорят, что сейчас слишком много подключений и отключают.
3. Каждый подключённый клиент имеет свой уникальный номер. При отключении и подключении номер обновляется.
   То есть, если  один клиент подключился, ему дали, допустим, номер 0, он отключился, после него никто не подключался, и он подключился снова, то у него будет номер 1.
4. В аргументах серверу передавать ip, port и путь до файла в таком порядке. Клиенту передавать ip и port.
5. Завершить работу сервера можно, отправив ему SIGTERM или SIGINT. Его pid выводится в stdout.
6. Если хочется закрыть клиент, то можно послать ему SIGTERM или SIGINT или нажать Ctrl+C из терминала, где он открыт.

**Пример использования:**
Для того чтобы запустить клиент и сервер, их нужно скомпилировать, например, с помощью команд
> gcc -o Server server.c

> gcc -o Client client.c

После этого, находясь в той же папке, запустить их можно с помощью команд(<> нужно убрать, они показывают, что вместо них нужно подставить какие-то значения)
> ./Server <аргументы>

> ./Client <аргументы>
