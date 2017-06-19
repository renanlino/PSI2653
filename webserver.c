#include <sys/socket.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include <libgen.h>
#include <pthread.h>
#include <semaphore.h>
#include <dirent.h>
#include <signal.h>
#include <wiringPi.h>
#include <math.h>
#include <stdbool.h>

#define BUFFERSIZE 1024
#define LINESIZE    80
#define PATHSIZE    80
#define FILESIZE    160
#define EXTSIZE     10
#define QLEN 10
#define NUMBER_OF_THREADS 100
#define min(x,y)   (int)((((int)x)<((int)y))?((int)x):((int)y))
#define SENSOR 0 	// SENSOR PIN
#define NB_SAMPLES_SENSOR 6

void gen_dirlist(char * html_response, char * path);
void * worker(void * arg);
void append(char *dest,int buffersize, char *src);
void lista_diretorio(char *path,char *buffer,int buffersize);
char * get_filename_ext(const char *filename);
int transferfile(char * path, int output_fd);
void gen_dirlist(char * html_response, char * path);
void * worker(void * arg);
void readSensor(int arg);



int			myport;
char		BASE[PATHSIZE];
int			sd;                          // Socket descriptor
int			status;                      // Estado da chamada
struct		sockaddr_in mylocal_addr;   // Meu endereço
char		rxbuffer[1024];
struct		sockaddr_in fromaddr;
int			size;
int			newsd;
struct		sockaddr_in clientaddr;
char		stringIP[20];
char		txbuffer[1024];
char		req[20];
char		ver[10];
char		str[50];
char		path[PATHSIZE];
char		file[FILESIZE];
sem_t		mutex;
sem_t		file_write;
sem_t		sensor;
sem_t   	persistentFiles;
pthread_t	threads[100];
int			thread_count = 0;
int 		LUMINOSIDADE = 0; 		// Variavel de luminosidade (sensor)


void append(char *dest,int buffersize, char *src) {
  int d;
  int i;

  d = strlen(dest);
  for (i=0; i<min(strlen(src),buffersize-1-d); i++) dest[d+i] = src[i];
  dest[d+i] = '\0';
}

// Lista diretorio
//    path: diretorio a ser listado
//    buffer: buffer que contera' a string com a sequencia ASCII
//            resultado da listagem do diretorio (finalizada por '\0'
//    bufffersize: tamanho do buffer

void lista_diretorio(char *path,char *buffer,int buffersize) {
  DIR           * dirp;
  struct dirent * direntry;

  dirp = opendir(path);
  if (dirp ==NULL) {
    perror("ERRO: chamada opendir(): Erro na abertura do diretorio: ");
    snprintf(buffer,buffersize,"Erro na listagem diretorio!\n");
    return;
  }
  buffer[0]='\0';
  while (dirp) {
    // Enquanto nao chegar ao fim do diretorio, leia cada entrada
    direntry = readdir(dirp);
    if (direntry == NULL) break; // chegou ao fim
    else {
     // ler entrada
     append(buffer,buffersize,direntry->d_name);
     append(buffer,buffersize,"\n");
     }
  }
  closedir(dirp);
}

char * get_filename_ext(const char *filename) {
    char *dot = strrchr(filename, '.');
    if(!dot || dot == filename) return "";
    return dot + 1;
}

int transferfile(char * path, int output_fd) {
	int          input_fd;     // input file descriptor
	int          status;
	int          n;
	char         ext[EXTSIZE];
	char         buffer[BUFFERSIZE];
	char         str[50];
	struct stat  statp;
	time_t rawtime;

	struct tm * timeinfo;

	input_fd = open(path,O_RDONLY);
	if (input_fd < 0) {
		strcpy(str, "HTTP/1.0 404 Not Found\n");
		write(output_fd, str, strlen(str));
		perror("ERRO chamada open(): Erro na abertura do arquivo: ");
		return(-1);
	}

	strcpy(ext, get_filename_ext(basename(path)));

	// Linha de status
	if ((!strcmp(ext,"html") || !strcmp(ext,"jpg") || !strcmp(ext,"png") || !strcmp(ext,"css") || !strcmp(ext,"js"))==0) {
		strcpy(str, "HTTP/1.0 400 Bad Request\n");
		write(output_fd, str, strlen(str));
		return(-1);
	}
	strcpy(str, "HTTP/1.0 200 OK\n");
	printf("%s",str);
	write (output_fd, str, strlen(str));

	// Data
	time (&rawtime);
	timeinfo = localtime (&rawtime);

	strftime (str,40,"Date: %a, %d %b %Y %T %Z\n",timeinfo);
	printf("%s",str);
	write (output_fd, str, strlen(str));

	// Server
	strcpy(str, "Server: Apache/1.3.0 (Unix)\n");
	printf("%s",str);
	write (output_fd, str, strlen(str));


	// Obtem stats do arquivo
	status = fstat(input_fd,&statp);
	if (status != 0) {
		perror("ERRO chamada stat(): Erro no acesso ao arquivo: ");
		status = close(input_fd);
		return(-1);
	}

	//Ultima modificaçao do arquivo

	timeinfo = localtime (&statp.st_mtim.tv_sec);

	strftime (str,40,"Last-Modified: %a, %d %b %Y %T %Z\n",timeinfo);
	printf("%s",str);
	write (output_fd, str, strlen(str));

	// obtem tamanho do arquivo
	sprintf(str,"Content-Length: %ld\n", statp.st_size);
	printf("%s",str);
	write(output_fd,str,strlen(str));

	// Tipo de conteudo
	if (!strcmp(ext, "html")) {
		strcpy(str, "Content-Type: text/html; charset=UTF-8\n\n");
		printf("%s",str);
	}

	else if (!strcmp(ext, "jpg")) {
		strcpy(str, "Content-Type: image/jpeg\n\n");
		printf("%s",str);
	}

	else if (!strcmp(ext, "png")) {
		strcpy(str, "Content-Type: image/png\n\n");
		printf("%s",str);
	}

  else if (!strcmp(ext, "css")) {
		strcpy(str, "Content-Type: text/css\n\n");
		printf("%s",str);
	}

  else if (!strcmp(ext, "js")) {
		strcpy(str, "Content-Type: text/javascript\n\n");
		printf("%s",str);
	}


	write(output_fd,str,strlen(str));

	// le arquivo , por partes
	do {
  	n = read(input_fd,buffer,BUFFERSIZE);
  	if (n<0) {
  	  perror("ERRO: chamada read(): Erro na leitura do arquivo: ");
  	  status = close(input_fd);
  	  return(-1);
  	}
  	write(output_fd,buffer,n);
	} while(n>0);

	status = close(input_fd);
	if (status == -1) {
		perror("ERRO: chamada close(): Erro no fechamento do arquivo: " );
		return(-1);
	}
	return(0);
}

int main() {
	FILE * config;
	int   status;
	char * nBASE;
	pthread_t sensor1;

	sem_init(&mutex, 0, 1);
	sem_init(&file_write, 0, 1);
	sem_init(&sensor, 0, 1);
  sem_init(&persistentFiles, 0, 1);

	signal(SIGPIPE,SIG_IGN);

	//le arquivo de config.
	config = fopen("config.txt", "r");
	fgets(BASE, PATHSIZE, config);
	myport = atoi(BASE);
	printf("port = %d\r\n", myport);
	fgets(BASE, PATHSIZE, config);

	nBASE = strtok(BASE, "\r");
	strcpy(BASE, nBASE);

	printf("PATH=%s\n", BASE);

	// Disparar threads de sensor e LED
	pthread_create(&sensor1, NULL, (void*) readSensor,1);



	// Conexão
	sd = socket(PF_INET, SOCK_STREAM, 0);
	if (sd == -1) {
		perror ("Erro na chamada socket");
		exit(1);
	}

	// Definição da porta onde o servidor vai aguardar
	mylocal_addr.sin_family = AF_INET;
	mylocal_addr.sin_addr.s_addr = INADDR_ANY;
	mylocal_addr.sin_port = htons(myport);

	status = bind(sd,
				(struct sockaddr *) &mylocal_addr,
				sizeof(struct sockaddr_in));
	if(status == -1) perror("Erro na chamada bind");

	// Abrir a porta onde o servidor vai escutar
	status = listen(sd,QLEN);
	if (status != 0) {
		perror("Erro na chamada listen");
		exit(1);
	}

	// Aceitar uma nova conexão TCP
	size = sizeof(clientaddr);

	while(1)
	{
		newsd = accept( sd,
				(struct sockaddr *) &clientaddr,
				(socklen_t *) &size);
		if (newsd < 0) {
			perror("Erro na chamada accept");
			exit(1);
		}

		sem_wait(&mutex);
		int * arg = malloc(sizeof(*arg));
		*arg = newsd;
		threads[thread_count] = pthread_create(&threads[thread_count], NULL, worker, arg);
		thread_count++;
		sem_post(&mutex);

		sem_wait(&sensor);
		printf("nivel de luminosidade: %d\n", LUMINOSIDADE);
		sem_post(&sensor);
	}

}

void httpSetVar(char * path, int mode) {
  FILE * persistentConfig;
  char data[7];
  sprintf(data, "%d\r\n", mode);
  sem_wait(&persistentFiles);
  persistentConfig = fopen(path, "w");
  if (persistentConfig != NULL) {
    fwrite(data, 1, sizeof(data), persistentConfig);
    fclose(persistentConfig);
    sem_post(&persistentFiles);
    printf("%s set as %s\r\n", path, data);
  } else {
    printf("httpSetVar error.\r\n");
  }
}

bool startsWith(const char *pre, const char *str)
{
    size_t lenpre = strlen(pre),
           lenstr = strlen(str);
    return lenstr < lenpre ? false : strncmp(pre, str, lenpre) == 0;
}

void * worker(void * arg) {
  int wsd = *((int *) arg);
	int status;
	char rxbuffer[BUFFERSIZE];
  char filename[BUFFERSIZE];
  int temp = 0;

  printf("\r\n############ WORKER ############\r\n");

	// Recepção mensagens
	while (temp < 1) {
		status = read(wsd,rxbuffer, sizeof(rxbuffer));
		if (status <= 0) perror("Erro na chamada read");

		printf("Mensagem recebida:\r\n%s\r\n", rxbuffer);
		sscanf(rxbuffer, "%s %s %s", req, path, ver);

		printf("REQUEST: %s\r\nPATH: %s\r\nVER:%s\r\n", req, path, ver);

    // Verificar req GET
		if (strcmp(req,"GET")!=0) {
			strcpy(str, "HTTP/1.0 400 Bad Request\n");
			printf("%s",str);
			write (wsd, str, strlen(str));
			perror("\tErro na request, nao é GET\r\n");
			temp++;
			continue;
		}

		// Verificar HTTP 1.0
		if (strcmp(ver,"HTTP/1.0")!=0) {
			strcpy(str, "HTTP/1.0 500 HTTP Version Not Supported\n");
			printf("%s",str);
			write (wsd, str, strlen(str));
			perror("\tErro na versao HTTP, nao é 1.0\r\n");
			temp++;
			continue;
		}

    if (strcmp(path, "/www/setMode?mode=0")==0) {
      httpSetVar("www/MODO.txt", 0);
      strcpy(str, "HTTP/1.0 204 No Content\r\n\r\n");
			printf("%s",str);
			write (wsd, str, strlen(str));
      temp++;
      continue;
    } else if (strcmp(path, "/www/setMode?mode=1")==0) {
      httpSetVar("www/MODO.txt", 1);
      strcpy(str, "HTTP/1.0 204 No Content\r\n\r\n");
			printf("%s",str);
			write (wsd, str, strlen(str));
      temp++;
      continue;
    } else if (strcmp(path, "/www/setState?state=0")==0) {
      httpSetVar("www/ESTADO.txt", 0);
      strcpy(str, "HTTP/1.0 204 No Content\r\n\r\n");
			printf("%s",str);
			write (wsd, str, strlen(str));
      temp++;
      continue;
    } else if (strcmp(path, "/www/setState?state=1")==0) {
      httpSetVar("www/ESTADO.txt", 1);
      strcpy(str, "HTTP/1.0 204 No Content\r\n\r\n");
			printf("%s",str);
			write (wsd, str, strlen(str));
      temp++;
      continue;
    } else if (startsWith("/www/setIntensity?intensity=", path)==true) {
      char * pch = strtok (path,"=");
      pch = strtok(NULL, "=");
      int intensity = atoi(pch);
      printf("Intensity sent = %s -> %d\r\n", pch, intensity);
      if (intensity >= 0 && intensity <= 100) {
        httpSetVar("www/INTENSIDADE.txt", intensity);
        strcpy(str, "HTTP/1.0 204 No Content\r\n\r\n");
  			printf("%s",str);
  			write (wsd, str, strlen(str));
        temp++;
        continue;
      }
    }

		strcpy(filename, BASE);
		strcat(filename, path);
		printf("\tCaminho solicitado: %s\n", filename);

		status = transferfile(filename, wsd);
		temp++;
		if (status == -1) {
      FILE * tmp;
      tmp = fopen( "temp.html", "w" );
      if (tmp != NULL ) {
        sem_wait(&file_write);
        char html_response[BUFFERSIZE];
        gen_dirlist(html_response, filename);
        fputs( html_response, tmp );
        fclose(tmp);
        status = transferfile("temp.html",wsd);
        sem_post(&file_write);
        if (status == -1) printf("\tErro na transferencia do arquivo [listdir]. \r\n");
      } else printf("\tErro na transferencia do arquivo [temp write]. \r\n");
	  }

  	status = close(wsd);
  	if (status == -1) perror("Erro na chamada close");
  	sem_wait(&mutex);
  	thread_count--;
  	sem_post(&mutex);
  }
  return NULL;
}

void gen_dirlist(char * html_response, char * path) {
  char ls[BUFFERSIZE];
  char link[BUFFERSIZE];
  char * entry;
  char * relativePath;
  unsigned int i;

  strcpy(html_response, "<html><head><title>Dir List</title></head><body>");

  for (i = 0; i < strlen(BASE) || i < strlen(path); i++) {
    if (BASE[i] != path[i]) break;
  }
  relativePath = &path[i];
  printf("Caminho relativo: %s\r\n", relativePath);

  printf("Listando o diretorio %s\r\n", path);
  lista_diretorio(path, ls, BUFFERSIZE);
  printf("%s\r\n", ls);

  entry = strtok(ls, "\n");
  while (entry != NULL) {
    strcpy(link, "<p><a href=\"");
    strcat(link, relativePath);
    strcat(link, "/");
    strcat(link, entry);
    strcat(link, "\">");
    strcat(link, entry);
    strcat(link, "</a></p>");

    strcat(html_response, link);
    entry = strtok(NULL, "\n");
  }
  strcat(html_response, "</body></html>");
  printf("HTML do diretorio: %s\r\n\r\n", html_response);
}

void readSensor(int arg)
{
	int lumMin = 0;
	int lumMax = 500000;
	int count;
	int k = 0;
	int i = 0;
	int q = 0;
	int mean = 0;
	int countList[NB_SAMPLES_SENSOR] = {0};
	//printf("test0\n");
	while (1)
	{
		//printf("test1\n");
		count = 0;
		wiringPiSetup () ;
		pinMode(SENSOR,OUTPUT);
		pullUpDnControl(SENSOR,PUD_OFF);
		digitalWrite(SENSOR,0);
		//printf("test2\n");
		sleep(1);

		pinMode(SENSOR,INPUT);

		while(digitalRead(SENSOR) == 0)
		{
			//printf("%d",count);
			count++;
			//sleep(0.1);
		}

		//printf("valor de count: %d\n", count);

		countList[k] = count;
		k++;
		
			
		
		if (k == NB_SAMPLES_SENSOR)
		{
			i = 1;
			k = 0;
		}
		//printf("k = %d e i = %d\n", k, i);
		/*for (q=0; q < NB_SAMPLES_SENSOR; q++)
		{
			printf("countList[%d] : %d", q, countList[q]);
		}
		*/
		
		printf("lumMin = %d e lumMax = %d\n", lumMin, lumMax);
		
			
		
		if (i == 1)
		{
			mean = 0;
			
			for (q = 0; q < NB_SAMPLES_SENSOR; q++)
			{
				mean = mean + countList[q];
			}
				
			mean = mean / NB_SAMPLES_SENSOR;


			if (mean < lumMax)
				lumMax = count;
				
			if (mean > lumMin)
				lumMin = count;
				
			sem_wait(&sensor);
			if (lumMin-lumMax != 0)
			{
				LUMINOSIDADE = (lumMin-count)*100/(lumMin-lumMax);
			}
			
			if (LUMINOSIDADE < 0)
				LUMINOSIDADE = 0;
			
			else if (LUMINOSIDADE > 100)
				LUMINOSIDADE = 100;

			printf("nivel de luminosidade: %d\n\n", LUMINOSIDADE);
			sem_post(&sensor);
		}

		usleep(500);

	}


}
