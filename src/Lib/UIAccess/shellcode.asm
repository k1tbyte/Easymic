.code

PUBLIC RemoteThreadFunc
PUBLIC RemoteThreadFuncSize

RemoteThreadFunc PROC
    ; Save registers
    push rbp
    mov rbp, rsp
    sub rsp, 50h  ; Space for local variables and stack alignment

    ; Save parameter (ShellcodeParams*)
    mov [rbp-8], rcx

    ; Get function pointers from structure
    mov rax, rcx
    mov rdx, [rax]      ; pfCreateWindowExA
    mov [rbp-16], rdx
    mov rdx, [rax+8]    ; pfGetMessageA
    mov [rbp-24], rdx
    mov rdx, [rax+16]   ; pfTranslateMessage
    mov [rbp-32], rdx
    mov rdx, [rax+24]   ; pfDispatchMessageA
    mov [rbp-40], rdx

    ; CreateWindowExA parameters from structure
    mov r10, [rbp-8]    ; params

    ; Prepare parameters for CreateWindowExA
    ; CreateWindowExA(exStyle, className, windowTitle, style, x, y, w, h, parent, menu, instance, lparam)

    push 0              ; lpParam
    push 0              ; hInstance
    push 0              ; hMenu
    push 0              ; hWndParent
    push 1              ; nHeight
    push 1              ; nWidth
    mov r9, 80000000h   ; Y = CW_USEDEFAULT
    push r9
    mov r9, 80000000h   ; X = CW_USEDEFAULT
    push r9

    mov r9d, [r10+36]   ; style
    lea r8, [r10+104]   ; windowTitle (offset 104)
    lea rdx, [r10+40]   ; className (offset 40)
    mov ecx, [r10+32]   ; exStyle

    call qword ptr [rbp-16]  ; Call CreateWindowExA
    add rsp, 40h        ; Clean up stack for 8 parameters

    test rax, rax
    jz exit_func

    ; Message loop
message_loop:
    lea rcx, [rbp-50h]  ; &msg (use space on stack)
    xor rdx, rdx        ; hwnd = NULL
    xor r8, r8          ; wMsgFilterMin = 0
    xor r9, r9          ; wMsgFilterMax = 0
    call qword ptr [rbp-24]  ; GetMessageA

    cmp rax, 0
    jle exit_func

    lea rcx, [rbp-50h]  ; &msg
    call qword ptr [rbp-32]  ; TranslateMessage

    lea rcx, [rbp-50h]  ; &msg
    call qword ptr [rbp-40]  ; DispatchMessageA

    jmp message_loop

exit_func:
    add rsp, 50h
    pop rbp
    ret

RemoteThreadFunc ENDP

RemoteThreadFuncEnd LABEL BYTE

RemoteThreadFuncSize DQ RemoteThreadFuncEnd - RemoteThreadFunc

END