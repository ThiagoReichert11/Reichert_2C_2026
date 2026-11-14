import tkinter as tk
from tkinter import ttk, scrolledtext, messagebox
import serial
import threading
import sqlite3
import re
import statistics
import time
import numpy as np
import matplotlib
matplotlib.use('TkAgg')
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg
import matplotlib.pyplot as plt

# --- CONFIGURACIÓN DEL PUERTO SERIE Y BASE DE DATOS ---
SERIAL_PORT = 'COM12'  # <-- Modifica según tu puerto (Ej. COM4, /dev/ttyUSB0)
BAUD_RATE = 115200
DB_NAME = 'mediciones_reaccion.db'

# Mapeo de comandos de color (Incluye Modo 5: Go/No-Go)
COLOR_MAP = {
    '1': 'Rojo',
    '2': 'Amarillo',
    '3': 'Verde',
    '4': 'Aleatorio',
    '5': 'Go/No-Go'
}

class MonitorUARTApp:
    def __init__(self, root):
        self.root = root
        self.root.title("Entrenador de Tiempo de Reacción - Monitor UART & Análisis")
        self.root.geometry("1250x900")

        # Configurar estilos globales de TTK para fuentes grandes
        self.setup_styles()

        # Variables de estado del examen
        self.exam_active = False
        self.exam_measurements = []
        self.exam_trial_count = 0
        self.exam_errors = 0
        self.current_color_code = '1'
        self.exam_dict = {}

        # Inicializar base de datos
        self.init_db()

        # --- PESTAÑAS PRINCIPALES (NOTEBOOK) ---
        self.notebook = ttk.Notebook(root)
        self.notebook.pack(fill=tk.BOTH, expand=True)

        self.tab_control = ttk.Frame(self.notebook)
        self.tab_stats = ttk.Frame(self.notebook)

        self.notebook.add(self.tab_control, text=" 🕹️ Control y Examen ")
        self.notebook.add(self.tab_stats, text=" 📊 Análisis Estadístico y Gráficas ")

        # Construir ambas pestañas
        self.build_control_tab()
        self.build_stats_tab()

        # Variables de conexión serie
        self.ser = None
        self.running = True

        # Cargar datos iniciales
        self.load_db_records()

        # Iniciar conexión serie
        self.conectar_serial()

    def setup_styles(self):
        """Aumenta sustancialmente el tamaño de las fuentes en componentes TTK."""
        style = ttk.Style()
        style.theme_use('default')

        # Estilo de las pestañas superiores
        style.configure("TNotebook.Tab", font=("Helvetica", 13, "bold"), padding=[14, 8])
        
        # Estilo de los encabezados y filas de las tablas (Treeview)
        style.configure("Treeview.Heading", font=("Helvetica", 13, "bold"))
        style.configure("Treeview", font=("Helvetica", 12), rowheight=32)
        
        # Estilo de las listas desplegables (Combobox)
        style.configure("TCombobox", font=("Helvetica", 12))

    # --- PESTAÑA 1: CONTROL Y EXAMEN ---
    def build_control_tab(self):
        top_frame = tk.LabelFrame(self.tab_control, text=" Configuración de Evaluación ", 
                                  font=("Helvetica", 13, "bold"), padx=14, pady=12)
        top_frame.pack(padx=10, pady=8, fill=tk.X)

        tk.Label(top_frame, text="Nombre:", font=("Helvetica", 13)).grid(row=0, column=0, padx=6, pady=6, sticky="w")
        self.entry_nombre = tk.Entry(top_frame, width=18, font=("Helvetica", 13))
        self.entry_nombre.insert(0, "Participante 1")
        self.entry_nombre.grid(row=0, column=1, padx=6, pady=6, sticky="w")

        tk.Label(top_frame, text="Color / Modo:", font=("Helvetica", 13)).grid(row=0, column=2, padx=6, pady=6, sticky="w")
        self.color_var = tk.StringVar(value='1')
        color_combo = ttk.Combobox(top_frame, textvariable=self.color_var, state="readonly", width=15, font=("Helvetica", 12))
        color_combo['values'] = ('1 - Rojo', '2 - Amarillo', '3 - Verde', '4 - Aleatorio', '5 - Go/No-Go')
        color_combo.current(0)
        color_combo.grid(row=0, column=3, padx=6, pady=6, sticky="w")
        color_combo.bind("<<ComboboxSelected>>", self.on_color_change)

        self.btn_exam = tk.Button(top_frame, text="▶ INICIAR EXAMEN (10 Ensayos)", 
                                  bg="#4CAF50", fg="white", font=("Helvetica", 12, "bold"), padx=12, pady=6,
                                  command=self.toggle_exam_mode)
        self.btn_exam.grid(row=0, column=4, padx=12, pady=6)

        btn_leaderboard = tk.Button(top_frame, text="🏆 LEADERBOARD", 
                                    bg="#FFD700", fg="black", font=("Helvetica", 12, "bold"), padx=12, pady=6,
                                    command=self.open_leaderboard)
        btn_leaderboard.grid(row=0, column=5, padx=12, pady=6)

        self.lbl_status = tk.Label(top_frame, text="Estado: Modo Normal (Inactivo)", 
                                   font=("Helvetica", 13, "italic"), fg="gray")
        self.lbl_status.grid(row=1, column=0, columnspan=6, padx=6, pady=8, sticky="w")

        center_frame = tk.Frame(self.tab_control)
        center_frame.pack(padx=10, pady=5, fill=tk.BOTH, expand=True)

        console_frame = tk.LabelFrame(center_frame, text=" Terminal UART ", font=("Helvetica", 13, "bold"))
        console_frame.pack(side=tk.LEFT, fill=tk.BOTH, expand=True, padx=(0, 5))
        
        # TERMINAL UART: Fuente ampliada a Consolas 15
        self.text_area = scrolledtext.ScrolledText(console_frame, wrap=tk.WORD, font=("Consolas", 15), width=35)
        self.text_area.pack(padx=6, pady=6, fill=tk.BOTH, expand=True)

        db_frame = tk.LabelFrame(center_frame, text=" Historial de Exámenes Recientes ", font=("Helvetica", 13, "bold"))
        db_frame.pack(side=tk.RIGHT, fill=tk.BOTH, expand=True, padx=(5, 0))

        columns = ("id", "nombre", "color", "promedio", "mediciones", "fecha")
        self.tree = ttk.Treeview(db_frame, columns=columns, show="headings", height=12)
        self.tree.heading("id", text="ID")
        self.tree.heading("nombre", text="Nombre")
        self.tree.heading("color", text="Color / Modo")
        self.tree.heading("promedio", text="Promedio (ms)")
        self.tree.heading("mediciones", text="Registros (ms)")
        self.tree.heading("fecha", text="Fecha / Hora")

        self.tree.column("id", width=45, anchor=tk.CENTER)
        self.tree.column("nombre", width=120)
        self.tree.column("color", width=110, anchor=tk.CENTER)
        self.tree.column("promedio", width=130, anchor=tk.CENTER)
        self.tree.column("mediciones", width=220)
        self.tree.column("fecha", width=150, anchor=tk.CENTER)
        self.tree.pack(padx=6, pady=6, fill=tk.BOTH, expand=True)

        btn_frame = tk.Frame(self.tab_control)
        btn_frame.pack(pady=12)

        tk.Button(btn_frame, text="⚡ Encender / Apagar Dispositivo (a)", 
                  bg="#e0e0e0", font=("Helvetica", 13, "bold"), padx=16, pady=8,
                  command=lambda: self.send_command('a')).pack()

    # --- PESTAÑA 2: ANÁLISIS ESTADÍSTICO Y GRÁFICAS ---
    def build_stats_tab(self):
        selector_frame = tk.LabelFrame(self.tab_stats, text=" Selección de Muestra ", 
                                       font=("Helvetica", 13, "bold"), padx=14, pady=12)
        selector_frame.pack(padx=10, pady=5, fill=tk.X)

        tk.Label(selector_frame, text="Seleccionar Examen Almacenado:", font=("Helvetica", 13, "bold")).pack(side=tk.LEFT, padx=6)
        
        self.combo_samples = ttk.Combobox(selector_frame, state="readonly", width=62, font=("Helvetica", 12))
        self.combo_samples.pack(side=tk.LEFT, padx=10)
        self.combo_samples.bind("<<ComboboxSelected>>", self.on_sample_selected)

        tk.Button(selector_frame, text="🔄 Actualizar", font=("Helvetica", 12, "bold"), command=self.load_db_records).pack(side=tk.LEFT, padx=6)

        metrics_frame = tk.LabelFrame(self.tab_stats, text=" Indicadores Estadísticos ", 
                                      font=("Helvetica", 13, "bold"), padx=16, pady=14)
        metrics_frame.pack(padx=10, pady=5, fill=tk.X)

        metrics_frame.columnconfigure(0, weight=1)
        metrics_frame.columnconfigure(1, weight=1)
        metrics_frame.columnconfigure(2, weight=1)

        self.lbl_stat_prom = tk.Label(metrics_frame, text="Promedio: -- ms", font=("Helvetica", 14, "bold"), fg="#1976D2")
        self.lbl_stat_prom.grid(row=0, column=0, sticky="w", pady=6)

        self.lbl_stat_media = tk.Label(metrics_frame, text="Media: -- ms", font=("Helvetica", 14, "bold"), fg="#00796B")
        self.lbl_stat_media.grid(row=0, column=1, sticky="w", pady=6)

        self.lbl_stat_median = tk.Label(metrics_frame, text="Mediana: -- ms", font=("Helvetica", 14, "bold"), fg="#388E3C")
        self.lbl_stat_median.grid(row=0, column=2, sticky="w", pady=6)

        self.lbl_stat_stdev = tk.Label(metrics_frame, text="Desviación Estándar: -- ms", font=("Helvetica", 14, "bold"), fg="#D32F2F")
        self.lbl_stat_stdev.grid(row=1, column=0, sticky="w", pady=6)

        self.lbl_stat_fatigue = tk.Label(metrics_frame, text="Índice de Fatiga / Acondicionamiento: --", font=("Helvetica", 14, "bold"), fg="#7B1FA2")
        self.lbl_stat_fatigue.grid(row=1, column=1, columnspan=2, sticky="w", pady=6)

        chart_frame = tk.LabelFrame(self.tab_stats, text=" Gráfico de Muestra con Línea de Tendencia ", 
                                    font=("Helvetica", 13, "bold"), padx=6, pady=6)
        chart_frame.pack(padx=10, pady=5, fill=tk.BOTH, expand=True)

        self.fig, self.ax = plt.subplots(figsize=(8.5, 4.2), dpi=100)
        self.ax.set_title("Seleccione una muestra arriba para visualizar la gráfica", fontsize=14)
        self.ax.grid(True, linestyle=':', alpha=0.6)

        self.canvas = FigureCanvasTkAgg(self.fig, master=chart_frame)
        self.canvas.get_tk_widget().pack(fill=tk.BOTH, expand=True)

    # --- BASE DE DATOS (SQLITE) ---
    def init_db(self):
        with sqlite3.connect(DB_NAME) as conn:
            cursor = conn.cursor()
            cursor.execute('''
                CREATE TABLE IF NOT EXISTS examenes (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    nombre TEXT NOT NULL,
                    color TEXT NOT NULL,
                    promedio REAL NOT NULL,
                    mediciones TEXT NOT NULL,
                    fecha TIMESTAMP DEFAULT CURRENT_TIMESTAMP
                )
            ''')
            conn.commit()

    def save_exam_record(self, nombre, color, promedio, mediciones_list):
        mediciones_str = ", ".join(map(str, mediciones_list))
        with sqlite3.connect(DB_NAME) as conn:
            cursor = conn.cursor()
            cursor.execute(
                "INSERT INTO examenes (nombre, color, promedio, mediciones) VALUES (?, ?, ?, ?)",
                (nombre, color, promedio, mediciones_str)
            )
            conn.commit()
        self.load_db_records()

    def load_db_records(self):
        for row in self.tree.get_children():
            self.tree.delete(row)

        combo_values = []
        self.exam_dict.clear()

        with sqlite3.connect(DB_NAME) as conn:
            cursor = conn.cursor()
            cursor.execute("""
                SELECT id, nombre, color, promedio, mediciones, 
                       strftime('%d/%m/%Y %H:%M:%S', fecha, 'localtime') 
                FROM examenes ORDER BY id DESC
            """)
            rows = cursor.fetchall()
            for record in rows:
                self.tree.insert("", tk.END, values=record)
                
                record_id, nombre, color, prom, meds_str, fecha = record
                label_text = f"ID #{record_id} | {nombre} | Modo: {color} | Promedio: {prom} ms | Fecha: {fecha}"
                combo_values.append(label_text)
                
                meds_parsed = []
                for x in meds_str.split(","):
                    val = x.strip()
                    if val.isdigit():
                        meds_parsed.append(int(val))

                self.exam_dict[label_text] = {
                    "nombre": nombre,
                    "color": color,
                    "mediciones": meds_parsed,
                    "fecha": fecha
                }

        self.combo_samples['values'] = combo_values
        if combo_values and not self.combo_samples.get():
            self.combo_samples.current(0)
            self.on_sample_selected()

    # --- CÁLCULOS ESTADÍSTICOS Y GRAFICACIÓN ---
    def on_sample_selected(self, event=None):
        selected_text = self.combo_samples.get()
        if not selected_text or selected_text not in self.exam_dict:
            return

        data = self.exam_dict[selected_text]
        meds = data["mediciones"]
        nombre = data["nombre"]
        color = data["color"]

        if not meds:
            self.lbl_stat_prom.config(text="Promedio: N/A")
            self.lbl_stat_media.config(text="Media: N/A")
            self.lbl_stat_median.config(text="Mediana: N/A")
            self.lbl_stat_stdev.config(text="Desviación Estándar: N/A")
            self.lbl_stat_fatigue.config(text="Índice de Fatiga / Acondicionamiento: N/A", fg="gray")
            self.ax.clear()
            self.ax.set_title(f"Sin datos gráficos para {nombre}", fontsize=14)
            self.canvas.draw()
            return

        # 1. Estadística Descriptiva
        mean_val = statistics.mean(meds)
        median_val = statistics.median(meds)
        stdev_val = statistics.stdev(meds) if len(meds) > 1 else 0.0

        # 2. Índice de Fatiga / Acondicionamiento (IF)
        if len(meds) >= 2:
            mid = len(meds) // 2
            first_half_mean = statistics.mean(meds[:mid])
            second_half_mean = statistics.mean(meds[mid:])
            
            fatigue_index = ((second_half_mean - first_half_mean) / first_half_mean) * 100.0

            if fatigue_index > 2.0:
                fatigue_str = f"Fatiga / Cansancio (+{fatigue_index:.1f}%)"
                fatigue_color = "#D32F2F"
            elif fatigue_index < -2.0:
                fatigue_str = f"Acondicionamiento / Aprendizaje ({fatigue_index:.1f}%)"
                fatigue_color = "#388E3C"
            else:
                fatigue_str = f"Rendimiento Estable ({fatigue_index:+.1f}%)"
                fatigue_color = "#1976D2"
        else:
            fatigue_str = "Insuficientes muestras"
            fatigue_color = "gray"

        # 3. Actualizar Etiquetas en Interfaz
        self.lbl_stat_prom.config(text=f"Promedio: {mean_val:.2f} ms")
        self.lbl_stat_media.config(text=f"Media: {mean_val:.2f} ms")
        self.lbl_stat_median.config(text=f"Mediana: {median_val:.2f} ms")
        self.lbl_stat_stdev.config(text=f"Desviación Estándar: {stdev_val:.2f} ms")
        self.lbl_stat_fatigue.config(text=f"Índice de Fatiga / Acondicionamiento: {fatigue_str}", fg=fatigue_color)

        # 4. Renderizar Gráfico con Matplotlib
        self.ax.clear()
        x_indices = np.array(range(1, len(meds) + 1))
        y_values = np.array(meds)
        
        self.ax.plot(x_indices, y_values, marker='o', linewidth=2.8, markersize=8, color='#1976D2', label='Tiempo de Reacción')
        self.ax.axhline(y=mean_val, color='#D32F2F', linestyle='--', linewidth=2.2, label=f'Media ({mean_val:.1f} ms)')

        if len(meds) > 1:
            m, b = np.polyfit(x_indices, y_values, 1)
            trend_line = m * x_indices + b
            self.ax.plot(x_indices, trend_line, linestyle=':', linewidth=2.6, color='#7B1FA2', 
                         label=f'Tendencia (m = {m:+.1f} ms/ensayo)')

        self.ax.set_title(f"Evaluación de {nombre} (Modo: {color})", fontsize=15, fontweight='bold')
        self.ax.set_xlabel("Número de Ensayo / Muestra Válida", fontsize=13, fontweight='bold')
        self.ax.set_ylabel("Tiempo de Reacción (ms)", fontsize=13, fontweight='bold')
        self.ax.set_xticks(x_indices)
        self.ax.tick_params(axis='both', which='major', labelsize=12)
        self.ax.grid(True, linestyle=':', alpha=0.6)
        self.ax.legend(loc="upper right", fontsize=12)
        
        self.fig.tight_layout()
        self.canvas.draw()

    # --- LEADERBOARD ---
    def open_leaderboard(self):
        lead_win = tk.Toplevel(self.root)
        lead_win.title("🏆 Leaderboard - Mejores Promedios")
        lead_win.geometry("750x520")

        lbl_title = tk.Label(lead_win, text="🥇 Clasificación General (Tiempos Más Rápidos)", 
                             font=("Helvetica", 15, "bold"), pady=16)
        lbl_title.pack()

        columns = ("pos", "nombre", "color", "promedio", "fecha")
        tree_lead = ttk.Treeview(lead_win, columns=columns, show="headings", height=12)
        
        tree_lead.heading("pos", text="Posición")
        tree_lead.heading("nombre", text="Participante")
        tree_lead.heading("color", text="Modo")
        tree_lead.heading("promedio", text="Promedio (ms)")
        tree_lead.heading("fecha", text="Fecha")

        tree_lead.column("pos", width=95, anchor=tk.CENTER)
        tree_lead.column("nombre", width=160)
        tree_lead.column("color", width=110, anchor=tk.CENTER)
        tree_lead.column("promedio", width=140, anchor=tk.CENTER)
        tree_lead.column("fecha", width=170, anchor=tk.CENTER)
        tree_lead.pack(padx=14, pady=8, fill=tk.BOTH, expand=True)

        with sqlite3.connect(DB_NAME) as conn:
            cursor = conn.cursor()
            cursor.execute("""
                SELECT nombre, color, promedio, strftime('%d/%m/%Y %H:%M:%S', fecha, 'localtime') 
                FROM examenes ORDER BY promedio ASC
            """)
            
            medals = ["🥇 1º", "🥈 2º", "🥉 3º"]
            for idx, record in enumerate(cursor.fetchall(), start=1):
                pos = medals[idx-1] if idx <= 3 else f"{idx}º"
                tree_lead.insert("", tk.END, values=(pos, record[0], record[1], record[2], record[3]))

    # --- LÓGICA DE MODO EXAMEN Y SERIE ---
    def on_color_change(self, event=None):
        val = self.color_var.get()
        self.current_color_code = val.split(" ")[0]
        self.send_command(self.current_color_code)

    def toggle_exam_mode(self):
        if self.exam_active:
            self.cancel_exam_mode()
        else:
            self.start_exam_mode()

    def start_exam_mode(self):
        nombre = self.entry_nombre.get().strip()
        if not nombre:
            messagebox.showwarning("Atención", "Por favor, ingrese un nombre de participante.")
            return

        self.exam_active = True
        self.exam_measurements = []
        self.exam_trial_count = 0
        self.exam_errors = 0
        
        self.send_command(self.current_color_code)
        self.send_command('a')

        self.btn_exam.config(text="⏹ CANCELAR EXAMEN", bg="#f44336")
        self.lbl_status.config(
            text=f"Modo Examen ACTIVO | Participante: {nombre} | Ensayo: 0/10 | Errores: 0", 
            fg="blue"
        )
        self.append_text(f"\n>>> INICIANDO MODO EXAMEN PARA: {nombre} <<<\n")

    def cancel_exam_mode(self):
        self.send_command('a')
        self.exam_active = False
        self.exam_measurements = []
        self.exam_trial_count = 0
        self.exam_errors = 0

        self.btn_exam.config(text="▶ INICIAR EXAMEN (10 Ensayos)", bg="#4CAF50")
        self.lbl_status.config(text="Estado: Examen Cancelado | Dispositivo Apagado", fg="red")
        self.append_text("\n>>> EXAMEN CANCELADO POR EL USUARIO. APAGANDO DISPOSITIVO... <<<\n")

    def finish_exam_mode(self):
        nombre = self.entry_nombre.get().strip() or "Anónimo"
        color_nombre = COLOR_MAP.get(self.current_color_code, 'Desconocido')
        
        if self.exam_measurements:
            promedio = round(sum(self.exam_measurements) / len(self.exam_measurements), 2)
        else:
            promedio = 0.0

        self.send_command('a')
        self.save_exam_record(nombre, color_nombre, promedio, self.exam_measurements)

        self.exam_active = False
        self.btn_exam.config(text="▶ INICIAR EXAMEN (10 Ensayos)", bg="#4CAF50")
        self.lbl_status.config(
            text=f"Examen Finalizado | Promedio: {promedio} ms | Errores: {self.exam_errors} | Dispositivo Apagado", 
            fg="green"
        )
        self.append_text(f"\n>>> EXAMEN FINALIZADO. PROMEDIO: {promedio} ms. ERRORES: {self.exam_errors}. APAGANDO... <<<\n")

        messagebox.showinfo(
            "Examen Completado", 
            f"¡Examen finalizado con éxito!\n\n"
            f"Participante: {nombre}\n"
            f"Modo: {color_nombre}\n"
            f"Promedio: {promedio} ms\n"
            f"Falsas Alarmas / Errores: {self.exam_errors}\n\n"
            f"El dispositivo se ha apagado automáticamente."
        )

    def conectar_serial(self):
        try:
            self.ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=1)
            self.append_text(f"[*] Conectado a {SERIAL_PORT}.\n")
            
            self.thread = threading.Thread(target=self.read_serial, daemon=True)
            self.thread.start()
        except serial.SerialException as e:
            self.append_text(f"[ERROR] No se pudo abrir el puerto {SERIAL_PORT}.\n")

    def send_command(self, cmd):
        if self.ser and self.ser.is_open:
            self.ser.write(cmd.encode('utf-8'))

    def read_serial(self):
        while self.running:
            if self.ser and self.ser.is_open:
                try:
                    if self.ser.in_waiting > 0:
                        linea = self.ser.readline().decode('utf-8', errors='ignore')
                        if linea:
                            self.root.after(0, self.process_line, linea)
                    else:
                        time.sleep(0.02)
                except Exception:
                    break
            else:
                time.sleep(0.1)

    def process_line(self, linea):
        self.append_text(linea)

        match_acierto = re.search(r"Tiempo de reacción:\s*(\d+)\s*ms", linea)
        match_error_red = re.search(r"¡ERROR!\s*Falsa alarma", linea)
        match_correct_red = re.search(r"¡CORRECTO!\s*Inhibición lograda", linea)
        match_omision_green = re.search(r"Tiempo agotado\.\s*Omisión", linea)

        if self.exam_active:
            trial_event = False

            if match_acierto:
                tiempo_ms = int(match_acierto.group(1))
                self.exam_measurements.append(tiempo_ms)
                trial_event = True
            elif match_error_red:
                self.exam_errors += 1
                self.exam_measurements.append(3000)
                trial_event = True
            elif match_correct_red:
                trial_event = True
            elif match_omision_green:
                self.exam_errors += 1
                self.exam_measurements.append(3000)
                trial_event = True

            if trial_event:
                self.exam_trial_count += 1
                count = self.exam_trial_count
                
                self.lbl_status.config(
                    text=f"Modo Examen ACTIVO | Ensayo: {count}/10 | Errores: {self.exam_errors}", 
                    fg="blue"
                )

                if count >= 10:
                    self.finish_exam_mode()

    def append_text(self, text):
        self.text_area.insert(tk.END, text)
        self.text_area.see(tk.END)

    def on_closing(self):
        self.running = False
        time.sleep(0.1)

        if self.ser:
            try:
                if self.ser.is_open:
                    self.ser.close()
            except Exception:
                pass

        try:
            plt.close('all')
        except Exception:
            pass

        self.root.quit()
        self.root.destroy()

if __name__ == "__main__":
    root = tk.Tk()
    app = MonitorUARTApp(root)
    root.protocol("WM_DELETE_WINDOW", app.on_closing)
    root.mainloop()