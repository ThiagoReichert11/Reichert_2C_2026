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

# Mapeo de comandos de color
COLOR_MAP = {
    '1': 'Rojo',
    '2': 'Amarillo',
    '3': 'Verde',
    '4': 'Aleatorio'
}

class MonitorUARTApp:
    def __init__(self, root):
        self.root = root
        self.root.title("Entrenador de Tiempo de Reacción")
        self.root.geometry("1100x820")

        # Configurar estilos globales de TTK
        self.setup_styles()

        # Variables de estado del examen
        self.exam_active = False
        self.exam_measurements = []
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
        """Aumenta el tamaño de las fuentes en componentes de TTK."""
        style = ttk.Style()
        style.theme_use('default')

        style.configure("TNotebook.Tab", font=("Helvetica", 11, "bold"), padding=[10, 5])
        style.configure("Treeview.Heading", font=("Helvetica", 11, "bold"))
        style.configure("Treeview", font=("Helvetica", 10), rowheight=26)
        style.configure("TCombobox", font=("Helvetica", 11))

    # --- PESTAÑA 1: CONTROL Y EXAMEN ---
    def build_control_tab(self):
        top_frame = tk.LabelFrame(self.tab_control, text=" Configuración de Evaluación ", 
                                  font=("Helvetica", 11, "bold"), padx=10, pady=8)
        top_frame.pack(padx=10, pady=5, fill=tk.X)

        tk.Label(top_frame, text="Nombre:", font=("Helvetica", 11)).grid(row=0, column=0, padx=5, pady=5, sticky="w")
        self.entry_nombre = tk.Entry(top_frame, width=18, font=("Helvetica", 11))
        self.entry_nombre.insert(0, "Participante 1")
        self.entry_nombre.grid(row=0, column=1, padx=5, pady=5, sticky="w")

        tk.Label(top_frame, text="Color Estímulo:", font=("Helvetica", 11)).grid(row=0, column=2, padx=5, pady=5, sticky="w")
        self.color_var = tk.StringVar(value='1')
        color_combo = ttk.Combobox(top_frame, textvariable=self.color_var, state="readonly", width=13, font=("Helvetica", 11))
        color_combo['values'] = ('1 - Rojo', '2 - Amarillo', '3 - Verde', '4 - Aleatorio')
        color_combo.current(0)
        color_combo.grid(row=0, column=3, padx=5, pady=5, sticky="w")
        color_combo.bind("<<ComboboxSelected>>", self.on_color_change)

        self.btn_exam = tk.Button(top_frame, text="▶ INICIAR EXAMEN (10 Ensayos)", 
                                  bg="#4CAF50", fg="white", font=("Helvetica", 10, "bold"), padx=8, pady=4,
                                  command=self.toggle_exam_mode)
        self.btn_exam.grid(row=0, column=4, padx=10, pady=5)

        btn_leaderboard = tk.Button(top_frame, text="🏆 LEADERBOARD", 
                                    bg="#FFD700", fg="black", font=("Helvetica", 10, "bold"), padx=8, pady=4,
                                    command=self.open_leaderboard)
        btn_leaderboard.grid(row=0, column=5, padx=10, pady=5)

        self.lbl_status = tk.Label(top_frame, text="Estado: Modo Normal (Inactivo)", 
                                   font=("Helvetica", 11, "italic"), fg="gray")
        self.lbl_status.grid(row=1, column=0, columnspan=6, padx=5, pady=4, sticky="w")

        center_frame = tk.Frame(self.tab_control)
        center_frame.pack(padx=10, pady=5, fill=tk.BOTH, expand=True)

        console_frame = tk.LabelFrame(center_frame, text=" Terminal UART ", font=("Helvetica", 11, "bold"))
        console_frame.pack(side=tk.LEFT, fill=tk.BOTH, expand=True, padx=(0, 5))
        
        self.text_area = scrolledtext.ScrolledText(console_frame, wrap=tk.WORD, font=("Consolas", 11), width=35)
        self.text_area.pack(padx=5, pady=5, fill=tk.BOTH, expand=True)

        db_frame = tk.LabelFrame(center_frame, text=" Historial de Exámenes Recientes ", font=("Helvetica", 11, "bold"))
        db_frame.pack(side=tk.RIGHT, fill=tk.BOTH, expand=True, padx=(5, 0))

        columns = ("id", "nombre", "color", "promedio", "mediciones", "fecha")
        self.tree = ttk.Treeview(db_frame, columns=columns, show="headings", height=12)
        self.tree.heading("id", text="ID")
        self.tree.heading("nombre", text="Nombre")
        self.tree.heading("color", text="Color")
        self.tree.heading("promedio", text="Promedio (ms)")
        self.tree.heading("mediciones", text="10 Mediciones (ms)")
        self.tree.heading("fecha", text="Fecha / Hora")

        self.tree.column("id", width=35, anchor=tk.CENTER)
        self.tree.column("nombre", width=100)
        self.tree.column("color", width=80, anchor=tk.CENTER)
        self.tree.column("promedio", width=110, anchor=tk.CENTER)
        self.tree.column("mediciones", width=200)
        self.tree.column("fecha", width=130, anchor=tk.CENTER)
        self.tree.pack(padx=5, pady=5, fill=tk.BOTH, expand=True)

        btn_frame = tk.Frame(self.tab_control)
        btn_frame.pack(pady=10)

        tk.Button(btn_frame, text="⚡ Encender / Apagar Dispositivo (a)", 
                  bg="#e0e0e0", font=("Helvetica", 11, "bold"), padx=12, pady=6,
                  command=lambda: self.send_command('a')).pack()

    # --- PESTAÑA 2: ANÁLISIS ESTADÍSTICO Y GRÁFICAS ---
    def build_stats_tab(self):
        # Panel Superior: Selector de Muestra
        selector_frame = tk.LabelFrame(self.tab_stats, text=" Selección de Muestra ", 
                                       font=("Helvetica", 11, "bold"), padx=10, pady=10)
        selector_frame.pack(padx=10, pady=5, fill=tk.X)

        tk.Label(selector_frame, text="Seleccionar Examen Almacenado:", font=("Helvetica", 11, "bold")).pack(side=tk.LEFT, padx=5)
        
        self.combo_samples = ttk.Combobox(selector_frame, state="readonly", width=60, font=("Helvetica", 10))
        self.combo_samples.pack(side=tk.LEFT, padx=10)
        self.combo_samples.bind("<<ComboboxSelected>>", self.on_sample_selected)

        tk.Button(selector_frame, text="🔄 Actualizar", font=("Helvetica", 10, "bold"), command=self.load_db_records).pack(side=tk.LEFT, padx=5)

        # Panel Métricas Estadísticas (Organización en Cuadrícula 3x2)
        metrics_frame = tk.LabelFrame(self.tab_stats, text=" Indicadores Estadísticos ", 
                                      font=("Helvetica", 11, "bold"), padx=15, pady=10)
        metrics_frame.pack(padx=10, pady=5, fill=tk.X)

        metrics_frame.columnconfigure(0, weight=1)
        metrics_frame.columnconfigure(1, weight=1)
        metrics_frame.columnconfigure(2, weight=1)

        self.lbl_stat_prom = tk.Label(metrics_frame, text="Promedio: -- ms", font=("Helvetica", 12, "bold"), fg="#1976D2")
        self.lbl_stat_prom.grid(row=0, column=0, sticky="w", pady=4)

        self.lbl_stat_media = tk.Label(metrics_frame, text="Media: -- ms", font=("Helvetica", 12, "bold"), fg="#00796B")
        self.lbl_stat_media.grid(row=0, column=1, sticky="w", pady=4)

        self.lbl_stat_median = tk.Label(metrics_frame, text="Mediana: -- ms", font=("Helvetica", 12, "bold"), fg="#388E3C")
        self.lbl_stat_median.grid(row=0, column=2, sticky="w", pady=4)

        self.lbl_stat_stdev = tk.Label(metrics_frame, text="Desviación Estándar: -- ms", font=("Helvetica", 12, "bold"), fg="#D32F2F")
        self.lbl_stat_stdev.grid(row=1, column=0, sticky="w", pady=4)

        self.lbl_stat_fatigue = tk.Label(metrics_frame, text="Índice de Fatiga / Acondicionamiento: --", font=("Helvetica", 12, "bold"), fg="#7B1FA2")
        self.lbl_stat_fatigue.grid(row=1, column=1, columnspan=2, sticky="w", pady=4)

        # Panel para el Gráfico Matplotlib
        chart_frame = tk.LabelFrame(self.tab_stats, text=" Gráfico de Muestra con Línea de Tendencia ", 
                                    font=("Helvetica", 11, "bold"), padx=5, pady=5)
        chart_frame.pack(padx=10, pady=5, fill=tk.BOTH, expand=True)

        self.fig, self.ax = plt.subplots(figsize=(8, 4), dpi=100)
        self.ax.set_title("Seleccione una muestra arriba para visualizar la gráfica", fontsize=12)
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
                label_text = f"ID #{record_id} | {nombre} | Color: {color} | Promedio: {prom} ms | Fecha: {fecha}"
                combo_values.append(label_text)
                self.exam_dict[label_text] = {
                    "nombre": nombre,
                    "color": color,
                    "mediciones": [int(x.strip()) for x in meds_str.split(",")],
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

        # 1. Estadística Descriptiva Básica
        mean_val = statistics.mean(meds)
        median_val = statistics.median(meds)
        stdev_val = statistics.stdev(meds) if len(meds) > 1 else 0.0

        # 2. Cálculo del Índice de Fatiga / Acondicionamiento (IF)
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

        # 3. Actualizar Etiquetas en Interfaz con todos los indicadores solicitados
        self.lbl_stat_prom.config(text=f"Promedio: {mean_val:.2f} ms")
        self.lbl_stat_media.config(text=f"Media: {mean_val:.2f} ms")
        self.lbl_stat_median.config(text=f"Mediana: {median_val:.2f} ms")
        self.lbl_stat_stdev.config(text=f"Desviación Estándar: {stdev_val:.2f} ms")
        self.lbl_stat_fatigue.config(text=f"Índice de Fatiga / Acondicionamiento: {fatigue_str}", fg=fatigue_color)

        # 4. Renderizar Gráfico y Ajuste Lineal
        self.ax.clear()
        x_indices = np.array(range(1, len(meds) + 1))
        y_values = np.array(meds)
        
        self.ax.plot(x_indices, y_values, marker='o', linewidth=2.5, markersize=7, color='#1976D2', label='Tiempo de Reacción')
        self.ax.axhline(y=mean_val, color='#D32F2F', linestyle='--', linewidth=1.8, label=f'Media ({mean_val:.1f} ms)')

        if len(meds) > 1:
            m, b = np.polyfit(x_indices, y_values, 1)
            trend_line = m * x_indices + b
            self.ax.plot(x_indices, trend_line, linestyle=':', linewidth=2.2, color='#7B1FA2', 
                         label=f'Tendencia (m = {m:+.1f} ms/ensayo)')

        self.ax.set_title(f"Evaluación de {nombre} (Estímulo: {color})", fontsize=13, fontweight='bold')
        self.ax.set_xlabel("Número de Ensayo", fontsize=11, fontweight='bold')
        self.ax.set_ylabel("Tiempo de Reacción (ms)", fontsize=11, fontweight='bold')
        self.ax.set_xticks(x_indices)
        self.ax.tick_params(axis='both', which='major', labelsize=10)
        self.ax.grid(True, linestyle=':', alpha=0.6)
        self.ax.legend(loc="upper right", fontsize=10)
        
        self.fig.tight_layout()
        self.canvas.draw()

    # --- LEADERBOARD ---
    def open_leaderboard(self):
        lead_win = tk.Toplevel(self.root)
        lead_win.title("🏆 Leaderboard - Mejores Promedios")
        lead_win.geometry("650x450")

        lbl_title = tk.Label(lead_win, text="🥇 Clasificación General (Tiempos Más Rápidos)", 
                             font=("Helvetica", 13, "bold"), pady=12)
        lbl_title.pack()

        columns = ("pos", "nombre", "color", "promedio", "fecha")
        tree_lead = ttk.Treeview(lead_win, columns=columns, show="headings", height=12)
        
        tree_lead.heading("pos", text="Posición")
        tree_lead.heading("nombre", text="Participante")
        tree_lead.heading("color", text="Color")
        tree_lead.heading("promedio", text="Promedio (ms)")
        tree_lead.heading("fecha", text="Fecha")

        tree_lead.column("pos", width=80, anchor=tk.CENTER)
        tree_lead.column("nombre", width=140)
        tree_lead.column("color", width=90, anchor=tk.CENTER)
        tree_lead.column("promedio", width=120, anchor=tk.CENTER)
        tree_lead.column("fecha", width=150, anchor=tk.CENTER)
        tree_lead.pack(padx=10, pady=5, fill=tk.BOTH, expand=True)

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
        
        self.send_command(self.current_color_code)
        self.send_command('a')

        self.btn_exam.config(text="⏹ CANCELAR EXAMEN", bg="#f44336")
        self.lbl_status.config(
            text=f"Modo Examen ACTIVO | Participante: {nombre} | Ensayo: 0/10", 
            fg="blue"
        )
        self.append_text(f"\n>>> INICIANDO MODO EXAMEN PARA: {nombre} <<<\n")

    def cancel_exam_mode(self):
        self.send_command('a')
        self.exam_active = False
        self.exam_measurements = []

        self.btn_exam.config(text="▶ INICIAR EXAMEN (10 Ensayos)", bg="#4CAF50")
        self.lbl_status.config(text="Estado: Examen Cancelado | Dispositivo Apagado", fg="red")
        self.append_text("\n>>> EXAMEN CANCELADO POR EL USUARIO. APAGANDO DISPOSITIVO... <<<\n")

    def finish_exam_mode(self):
        nombre = self.entry_nombre.get().strip() or "Anónimo"
        color_nombre = COLOR_MAP.get(self.current_color_code, 'Desconocido')
        
        promedio = round(sum(self.exam_measurements) / len(self.exam_measurements), 2)

        self.send_command('a')
        self.save_exam_record(nombre, color_nombre, promedio, self.exam_measurements)

        self.exam_active = False
        self.btn_exam.config(text="▶ INICIAR EXAMEN (10 Ensayos)", bg="#4CAF50")
        self.lbl_status.config(
            text=f"Examen Finalizado | Promedio: {promedio} ms | Dispositivo Apagado", 
            fg="green"
        )
        self.append_text(f"\n>>> EXAMEN FINALIZADO. PROMEDIO: {promedio} ms. APAGANDO DISPOSITIVO... <<<\n")

        messagebox.showinfo(
            "Examen Completado", 
            f"¡Examen finalizado con éxito!\n\n"
            f"Participante: {nombre}\n"
            f"Color: {color_nombre}\n"
            f"Promedio: {promedio} ms\n\n"
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

        match = re.search(r"Tiempo de reacción:\s*(\d+)\s*ms", linea)
        if match:
            tiempo_ms = int(match.group(1))

            if self.exam_active:
                self.exam_measurements.append(tiempo_ms)
                count = len(self.exam_measurements)
                
                self.lbl_status.config(
                    text=f"Modo Examen ACTIVO | Ensayo: {count}/10 | Último: {tiempo_ms} ms", 
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