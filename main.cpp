#include <cstdlib>
#include <iostream>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <pthread.h>
#include <fstream>
#include <fcntl.h>
#include <cstring>
#include <vector>

/*
Алгоритм:
	1.	Прочитать параметры командной строки, распаковать их в структуру.
	2.	Прочитать файл c открытым текстом в бинарном виде, отобразить в оперативную память. Получить размер файла. Предусмотреть ограничение на размер файла.
	3.	Создать ПСП по прочитанным параметрам с помощью ЛКГ в отдельном потоке pthread API.
	4.	Синхронизировать основной поток с рабочим с помощью присоединения.
	5.	Создать барьер.
	6.	Задекларировать структуру — контекст, содержащую барьер, входные данные для каждого воркера (фрагменты блокнота и открытого текста), а также предусматривающую получение выходных данных от воркера.
	7.	Создать N воркеров с помощью функции pthread_create(), передав каждому экземпляр контекста.
	8.	Главный поток блокируется по ожиданию барьера.
	9.	Каждый воркер производит побитовое сложение по модулю 2 своих фрагментов блокнота и текста и  блокируется по ожиданию барьера.
	10.	Когда счётчик n барьера становится равен N + 1 (n = N + 1), главный поток разблокируется и продолжает работу.
	11.	Главный поток объединяет и сохраняет данные в выходной файл.
	12.	Барьер уничтожается.
*/

using std::cout;

enum States{
	ERROR_CREATE_THREAD = 100,
	ERROR_BARRIER,
	ERROR_BARRIER_DESTROY,
	ERROR_WAIT_BARRIER,
	ERROR_JOIN_THREAD,
	ERROR_FILE_R_OPEN,
	ERROR_FILE_W_OPEN,
	ERROR_FILE,
	SUCCESS = 0
};

struct ProgramArguments{
	size_t a;				// множитель
	size_t c;				// приращение
	size_t m;				// модуль
	size_t seed; 			// x0 - начальное значение
	char* inputFilePath;	// путь до файла с входными значениями
	char* outputFilePath;	// путь до файла с выходными значениями
};

struct KeyGenParams{
	size_t a; 		// множитель
	size_t c;		// приращение
	size_t m;		// модуль
	size_t seed; 	// x0 - начальное значение
	size_t sizeKey; // размер файла
};

struct CryptParams
{
	char* msg;
	char* key;
	char* outputText;
	size_t size;
	size_t downIndex; 	// нижний индекс фрагмента
	size_t topIndex;	// верхний индекс фрагмента
	pthread_barrier_t* barrier;
};
// подготовка к генерации ЛКГ и генерация ЛКГ
void* keyGenerate(void* params){ 
	KeyGenParams *parametrs = reinterpret_cast<KeyGenParams *>(params);

	size_t a = parametrs->a;
	size_t m = parametrs->m;
	size_t c = parametrs->c;
	size_t sizeKey = parametrs->sizeKey;


	int* buff = new int[(sizeKey+1)/sizeof(int)];
	buff[0] = parametrs->seed;

	// ЛКГ генерация чисел
	// X[n+1] = (aX[n] + C) mod m
	for(size_t i = 1; i < (sizeKey + 1)/sizeof(int) ; i++){
		buff[i]= (a * buff[i-1] + c) % m; 
	}

	return reinterpret_cast<char *>(buff);
};

void* crypt(void * CryptParamsetrs)
{
	int status = 0;

	CryptParams* param = reinterpret_cast<CryptParams*>(CryptParamsetrs); //Жестко указываем компилятору тип
	size_t topIndex = param->topIndex;
	size_t downIndex = param->downIndex;

	while(downIndex < topIndex){
		param->outputText[downIndex] = param->key[downIndex] ^ param->msg[downIndex];
		downIndex++;
	}

	status = pthread_barrier_wait(param->barrier); 
	
	if(status != PTHREAD_BARRIER_SERIAL_THREAD && status != 0)
		{
		std::cout << "Problem with pthread_barrier_wait";
		exit(ERROR_WAIT_BARRIER);
		delete param;
		}
		return 0;
}

void freeSpace(char* outputText,char* msg,char* key){
	
	if(outputText != nullptr){
		delete[] outputText;
	}
	if(msg != nullptr){
		delete[] msg;
	}
	if (key != nullptr){
		delete[] key;
	}


}

int main (int argc, char **argv) {
	// 1.	Прочитать параметры командной строки, распаковать их в структуру.
	int c;
	ProgramArguments progParam;
	while ((c = getopt(argc, argv, "i:o:a:c:x:m:")) != -1) { // Разбор флагов
		switch (c) {
		case 'i':
			printf ("Flag i with value '%s'\n", optarg);
			progParam.inputFilePath = optarg;
			break;
		case 'o':
			printf ("Flag o with value '%s'\n", optarg);
			progParam.outputFilePath = optarg;
			break;
		case 'a':
			printf ("Flag a with value '%s'\n", optarg);
			progParam.a = atoi(optarg);
			break;
		case 'c':
			printf ("Flag c with value '%s'\n", optarg);
			progParam.c = atoi(optarg);
			break;
		case 'm':
			printf ("Flag m with value '%s'\n", optarg);
			progParam.m = atoi(optarg);
			break;
		case 'x':
			printf ("Flag x with value '%s'\n", optarg);
			progParam.seed = atoi(optarg);
			break;
		case '?':
			break;
		default:
			printf ("?? getopt returned character code 0%o ??\n", c);
		}
	}
	if (optind < argc) {
		printf ("Warning! The program received unnecessary arguments: ");
		while (optind < argc)
			printf ("%s ", argv[optind++]);
		printf ("\n");
	}
	
	int num_thread = sysconf(_SC_NPROCESSORS_ONLN); // Количествео процессоров
	cout<<"Count of available processors: " << num_thread << std::endl;
	int inputFile = open(progParam.inputFilePath, O_RDONLY); // open to read inputfile

	if (inputFile == -1) // Проверка на корректность отрытия файла
	{
		std::cerr << "Error with input file!";
		exit(ERROR_FILE_R_OPEN);
	}

	int inputSize = lseek(inputFile, 0, SEEK_END); // Узнаём размер файла в байтах
	std::cout<<"Size of input file:  = "<<inputSize<<std::endl; 

	if(inputSize == -1)
	{
		std::cout << "Error with calculation size of input file!";
		exit(ERROR_FILE);
	}

	char* key = nullptr;
	char* outputText = new char[inputSize]; 	//Зашифрованный текст
	char* msg = new char[inputSize]; 			// Текст из inputFile

	if(lseek(inputFile, 0, SEEK_SET) == -1) 	// Возвращаемся в начало файла, чтобы прочитать его с начала
	{
		std::cout << "Error with file!";
		freeSpace(outputText,msg,key);
		exit(ERROR_FILE);
	}

	inputSize = read(inputFile, msg, inputSize); //Помещаем в  msg текст из inputFile 

	if(inputSize == -1) // Проверка успешности перемещения  в буффер
	{
		std::cout << "Error in moving inputfile to buffer!";
		freeSpace(outputText,msg,key);
		exit(ERROR_FILE);
	}

	KeyGenParams keyParam;
	keyParam.sizeKey = inputSize;
	keyParam.a=progParam.a;
	keyParam.c=progParam.c;
	keyParam.m=progParam.m;
	keyParam.seed=progParam.seed;

	pthread_t keyGenThread;				//создаём отдельный поток для ЛКГ
	pthread_t cryptThread[num_thread];	// создаём массив потоков для шифрования 
	int status = 0;

	if(pthread_create(&keyGenThread, NULL, keyGenerate, &keyParam) != 0) //Создаем и проверяем успешность потока
	{
		std::cout << "Error with pthread_create()";
		freeSpace(outputText,msg,key);
		exit(ERROR_CREATE_THREAD);
	}
	if(pthread_join(keyGenThread, (void**)&key) != 0)// Запускаем поток и отлавливаем результат работы потока keyGenThread
	{
		std::cout << "Error with pthread_join()";
		freeSpace(outputText,msg,key);
		exit(ERROR_JOIN_THREAD);
	}
	// 5.	Создать барьер
	pthread_barrier_t barrier;

	status = pthread_barrier_init(&barrier, NULL, num_thread+1); //Задаём параметры для работы барьера 

	if(status != 0)// Проверяем успешность инициализации работы барьера
	{
		std::cout << "Error with pthread_barrier_init()";
		freeSpace(outputText,msg,key);
		exit(ERROR_BARRIER);
	}


	std::vector<CryptParams*> cryptPar;

	//Данная "случайность" зависит от количества ядер, и она же помогает нам 
	//производить обратную трансляцию. (шифр Вермана).
	// 7.	Создать N воркеров с помощью функции pthread_create(), передав каждому экземпляр контекста.
	for(int i = 0; i < num_thread ; i++)
	{
		// 6.	Задекларировать структуру — контекст, содержащую барьер, входные данные для каждого воркера (фрагменты блокнота и открытого текста), а также предусматривающую получение выходных данных от воркера.
		CryptParams* CryptParamsetrs = new CryptParams;

		CryptParamsetrs->key = key;
		CryptParamsetrs->size = inputSize;
		CryptParamsetrs->outputText = outputText;
		CryptParamsetrs->msg = msg;
		CryptParamsetrs->barrier = &barrier;

        size_t current_len = inputSize / num_thread;
        
        CryptParamsetrs->downIndex = i * current_len;
        CryptParamsetrs->topIndex = i * current_len + current_len;

        if (i == num_thread - 1)
        {
            CryptParamsetrs->topIndex = inputSize;
        }

		cryptPar.push_back(CryptParamsetrs);
		pthread_create(&cryptThread[i], NULL, crypt, CryptParamsetrs);

	}
	// синхронизация потоков
	status = pthread_barrier_wait(&barrier);

	int output;
	if ((output=open(progParam.outputFilePath, O_WRONLY))==-1) {
		printf ("Cannot open file.\n");
		exit(ERROR_FILE_W_OPEN);
	}
	if(write(output, outputText, inputSize) !=inputSize)
		printf("Write Error");
	close(output);

	// очистка
	pthread_barrier_destroy(&barrier);

	delete[] key;
	delete[] outputText;
	delete[] msg;

	for(auto & tempCryptParams : cryptPar){
		delete tempCryptParams;
	}
	cryptPar.clear();
	return SUCCESS;
}
