#include "exemplo_aula_05_11.h"

int main()
{
    
    config_app();
    
    xTaskCreate(tarefa_1, "AZUL", 128, NULL, 3, NULL);
    xTaskCreate(tarefa_2, "VERDE", 128, NULL, 3, NULL);
    xTaskCreate(tarefa_3, "VERMELHO", 128, NULL, 3, NULL);
    
    vTaskStartScheduler();
    
    while (1);
    
    return 0;
}

