#include "MEModel.hpp"

/* コンストラクタ */
MEModel::MEModel(int maxN_gram, int pattern_count_bias,
                 int max_iteration_learn, double epsilon_learn,
	            	 int max_iteration_f_select, double epsilon_f_select,
		             int max_iteration_f_gain, double epsilon_f_gain)
{
  this->maxN_gram              = maxN_gram;
  this->pattern_count_bias     = pattern_count_bias;
  this->max_iteration_learn    = max_iteration_learn;
  this->epsilon_learn          = epsilon_learn;
  this->max_iteration_f_select = max_iteration_f_select;
  this->epsilon_f_select       = epsilon_f_select;
  this->max_iteration_f_gain   = max_iteration_f_gain;
  this->epsilon_f_gain         = epsilon_f_gain;
  unique_word_no               = 0;
  pattern_count                = 0;
}

/* デストラクタ. */
MEModel::~MEModel(void)
{
}

/* 現在読み込み中のファイルから次の単語を返すサブルーチン */
std::string MEModel::next_word(void)
{
  char ch;          /* 現在読んでいる文字 */
  std::string ret;  /* 返り値の文字列 */

  /* 改行している場合は一行読む */
  if (line_index == -1) {
    if (input_file && std::getline(input_file, line_buffer)) {
      /* 注：std::getlineは改行文字をバッファに格納しない */
      line_index = 0; /* インデックスは0にリセット */
    } else {
      ret += "\EOF";
      return ret;     /* ファイルの終端に達している時はEOFだけからなる文字列を返す */
    }
  }
  
  /* とりあえず1文字目を読む */
  ch = line_buffer[line_index];

  /* 空白, タブ文字, その他区切り文字の読み飛ばし */
  while (ch == ' ' || ch == '\t')
    ch = line_buffer[++line_index];

  /* 行バッファの末尾（改行）に到達しているかチェック. */
  if (ch == '\0') {
    /* 到達していたらインデックスをリセットして, 再び呼び出す */
    line_index = -1;
    return next_word();
  }

  /* 次の空白/終端文字が現れるまでバッファに文字を読む */
  while (ch != ' ' && ch != '\t' && ch != '\0') {
    ret += ch;
    ch = line_buffer[++line_index];
  }

  return ret;
}

/* 引数文字列のテキストファイルをオープンし, 次を行う
   ・単語ハッシュの作成/更新
   ・素性候補の作成
   ・素性の頻度カウント
*/
void MEModel::read_file(std::string file_name)
{
  std::string str_buf;   /* 単語バッファ */
  std::vector<int> Ngram_buf(maxN_gram); /* 今と直前(maxN_gram-1)個の単語列. Ngram_buf[maxN_gram-1]が今の単語, Ngram_buf[0]が(maxN_gram-1)個前の単語 */
  std::vector<MEFeature>::iterator f_it; /* 素性のイテレータ */
  int file_top_count;                    /* ファイル先頭分の読み飛ばし. */

  /* ファイルのオープン */
  input_file.open(file_name.c_str(), std::ios::in);
  if ( !input_file ) {
    std::cerr << "Error : cannot open file \"" << file_name << "\"." << std::endl;
    return;
  }
  
  /* ファイルの終端まで単語を集める */
  file_top_count = 0;
  while ( (str_buf=next_word()) != "\EOF" ) {
    /* 今まで見たことがない（新しい）単語か判定 */
    if (word_map.count(str_buf) == 0) {
      /* 新しい単語ならば, マップに登録 */
      word_map[str_buf] = unique_word_no++;
      /* std::cout << "word \"" << str_buf << "\" assined to " << word_map[str_buf] << std::endl; */
    } 

    /* Ngram_bufの更新 */
    for (int n_gram=0; n_gram < (maxN_gram-1); n_gram++) {
      Ngram_buf[n_gram] = Ngram_buf[n_gram+1];
    }
    Ngram_buf[maxN_gram-1] = word_map[str_buf];

    /* 新しいパターンか判定.
       Ngram_bufの長さを変えながら見ていく. */
    for (int gram_len=0; gram_len < maxN_gram; gram_len++) {

      /* x, y のパターンをベクトルで取得 */
      int buf_y_pattern = Ngram_buf[maxN_gram-1];
      std::vector<int> buf_x_pattern(gram_len);
      for (int ii=0; ii < gram_len; ii++) {
        buf_x_pattern[ii] = Ngram_buf[(maxN_gram-1)-gram_len+ii];
      }

      /* ファイル先頭分の読み飛ばし. */
      if (file_top_count < gram_len) {
        file_top_count++;
        break;
      }

      /* 現在の素性集合を走査し,
         既に同じパターンの素性があるかチェック */
      for (f_it = candidate_features.begin();
          f_it != candidate_features.end();
          f_it++) {
        /* 完全一致で確かめる. 完全一致でないと短いグラムモデルにヒットしやすくなってしまう */
        if (f_it->strict_check_pattern(buf_x_pattern,
                                       buf_y_pattern) == true) {
          /* 同じパターンがあったならば, 頻度カウントを更新 */
          f_it->count++;
          break;
        }

      }

      /* 既出のパターンではなかった -> 新しく素性集合に追加 */
      if (f_it == candidate_features.end()
          && pattern_count < MAX_CANDIDATE_F_SIZE) {
        /* 新しい素性を追加 */
        candidate_features.push_back(MEFeature(gram_len+1,
                                               buf_x_pattern,
                                               buf_y_pattern));
        /* 新しいXパターンの追加を試みる */
        setX.insert(buf_x_pattern);
        /* パターン数の増加 */
        pattern_count++;
      }

    }

  }

  /* ファイルのクローズ */
  input_file.close();

}

/* ファイル名の配列から学習データをセット.
   得られた素性リストに経験確率と経験期待値をセットする */
void MEModel::read_file_str_list(std::vector<std::string> filenames)
{

  std::vector<std::string>::iterator file_it;
  /* read_fileを全ファイルに適用 */
  for (file_it = filenames.begin();
       file_it != filenames.end();
       file_it++) {
    read_file(*file_it);
  }

  /* 経験確率と経験期待値をセット */
  set_empirical_prob_E();

  /* TODO:FIXME:以下はテスト用 */
  copy_candidate_features_to_model_features();
  calc_normalized_factor();
  calc_model_prob();

  std::cout << "There are " << pattern_count << " unique patterns." << std::endl;
}

/* 経験確率/経験期待値を素性にセットする */
void MEModel::set_empirical_prob_E(void)
{
  std::vector<MEFeature>::iterator f_it, exf_it;   /* 素性のイテレータ */
  std::set<std::vector<int> >      find_x_pattern; /* 計算済みのXのパターン */
  int sum_count;                                   /* 出現した素性頻度総数 */

  /* 頻度総数のカウント. */
  /* TODO:バイアスpattern_count_biasの考慮 */
  sum_count = 0;
  for (f_it = candidate_features.begin();
       f_it != candidate_features.end();
       f_it++) {
    sum_count += f_it->count;
  }

  if (sum_count == 0) {
    std::cerr << "Error : total number of features frequency equal to 0. maybe all of feature's frequency smaller than bias" << std::endl;
    exit(1);
  }

  /* 経験確率のセット. 頻度を総数で割るだけ. */
  for (f_it = candidate_features.begin();
       f_it != candidate_features.end();
       f_it++) {
    f_it->empirical_prob
      = (double)(f_it->count) / sum_count;
    f_it->empirical_E = 0.0f;                       /* 期待値のリセット */
    empirical_x_prob[f_it->get_pattern_x()] = 0.0f; /* xの周辺経験分布P~(x)の初期化 */
  }

  /* 経験期待値/xの周辺経験分布のセット.
     注) 経験確率分布は候補素性のパターンのみで総和をとる（それで全確率） 
         真にXとYの組み合わせを試すと異なる結果になる事に注意 */
  for (f_it = candidate_features.begin();
       f_it != candidate_features.end();
       f_it++) {

    for (exf_it = candidate_features.begin();
	 exf_it != candidate_features.end();
	 exf_it++) {
      /* 経験期待値の計算 */
      f_it->empirical_E 
	+= f_it->checkget_weight_emprob(exf_it->get_pattern_x(),
					exf_it->get_pattern_y());
      
      /* 周辺経験分布P~(x)の計算:同じパターンxの経験確率を一度のみ足す. */
      if (exf_it->get_pattern_x() == f_it->get_pattern_x()) {
	/* Xのパターンでまだ発見されてなければ, 和を取る */
	if (find_x_pattern.count(exf_it->get_pattern_x()) == 0) {
	  empirical_x_prob[exf_it->get_pattern_x()] 
	    += exf_it->empirical_prob;
	}
      }
    }
    
    /* 発見したXのパターンの集合に追加 */
    find_x_pattern.insert(f_it->get_pattern_x());
  }

}

/* 周辺素性フラグのセット/更新 */
void MEModel::set_marginal_flag(void)
{
  std::vector<MEFeature>::iterator f_it, ex_f_it; /* 素性のイテレータ */
  std::set<std::vector<int> >::iterator x_it;     /* Xのパターンのイテレータ */
  int pattern_y;                          /* 一つのパターン */
  std::vector<int> pattern_x, pattern_w;  /* 異なるxのパターン. */
  bool is_find;

  /* 計算法 : 一つのパターンに対して, 他のpattern_wを持ってきたときに, 素性が異なる値をとった時, 素性は条件付き素性 */
  for (f_it = features.begin();
       f_it != features.end();
       f_it++) {

    is_find = false;
    /* パターン生成時は候補素性から */
    for (ex_f_it = candidate_features.begin();
        ex_f_it != candidate_features.end();
        ex_f_it++) {
      /* パターンの取得 */
      pattern_x = ex_f_it->get_pattern_x();
      pattern_y = ex_f_it->get_pattern_y();

      for (x_it = setX.begin();
           x_it != setX.end();
           x_it++) {
        /* 条件付き素性判定 */
        if (f_it->checkget_weight(pattern_x, pattern_y)
            !=
            f_it->checkget_weight(*x_it, pattern_y)) {
          f_it->is_marginal = false;
          is_find = true;
          break;
        }
	
      }

      /* 判定済みならばループを抜ける */
      if (is_find) break;
    }

    /* 反例が見つかってなければ, 周辺素性 */
    if (!is_find) f_it->is_marginal = true;
  }
    
}

/* 引数のパターンでの, 全てのモデル素性の(パラメタ*重み)和(=エネルギー関数値)を計算して返す */
double MEModel::get_sum_param_weight(std::vector<int> test_x, int test_y)
{
  std::vector<MEFeature>::iterator f_it;
  double sum;

  sum = 0;
  for (f_it = features.begin();
       f_it != features.end();
       f_it++) {
    sum += f_it->checkget_param_weight(test_x, test_y);
  }

  return sum;
}

/* 正規化項の計算 */
void MEModel::calc_normalized_factor(void)
{
  double marginal_factor;                /* 周辺素性の性質から計算できる項の値 */
  std::vector<MEFeature>::iterator f_it; /* 素性のイテレータ */
  std::set<int>::iterator y_m, y_x;           /* 周辺素性を活性化させる要素の集合Ym, 条件付き素性を活性化させる要素の集合Y(x)のイテレータ */
  std::set<std::vector<int> >::iterator x_it; /* Xのパターンのイテレータ */

  /* まず周辺素性のフラグをセット */
  set_marginal_flag();
  /* Yの分割も行う */
  set_setY();

  /* 結合確率の正規化項の計算... FIXME:嘘!! 全部のパターン和は不可能!! */
  joint_norm_factor = 0.0f;
  for (x_it = setX.begin(); x_it != setX.end(); x_it++) {
    for (int word_y = 0; word_y < unique_word_no; word_y++) {
      /* エネルギー関数値の計算 */
      double energy = 0.0f;
      for (f_it = features.begin();
	   f_it != features.end();
	   f_it++) {
	energy += f_it->checkget_param_weight(*x_it, word_y);
      }
      joint_norm_factor += exp(energy);
    }
  }

  /* 周辺素性の素性から計算できる分marginal_factorを計算 */
  marginal_factor = unique_word_no - setY_marginal.size(); /* |Y-Ym| */
  /* Ymについての和 */
  for (y_m = setY_marginal.begin();
       y_m != setY_marginal.end();
       y_m++) {
    double energy_z_y = 0.0f; /* z(y)のexp内部の値 */
    for (f_it = features.begin();
	 f_it != features.end();
	 f_it++) {
      if (f_it->is_marginal &&
	  f_it->get_pattern_y() == *y_m) {
	/* 周辺素性, かつyのパターンが一致して活性化している素性の
	 　エネルギー関数値を加算 */
	energy_z_y += f_it->parameter * f_it->weight;
      }
    }
    /* z(y_m)の値を加算 */
    marginal_factor += exp(energy_z_y);
  }

  /* 以下, 各Z(x)を計算していく */
  for (x_it = setX.begin(); x_it != setX.end(); x_it++) {
    /* 周辺素性による値で初期化 */
    norm_factor[*x_it] = marginal_factor;
    
    /* Y(x)についての和 */
    for (y_x = setY_cond[*x_it].begin();
	 y_x != setY_cond[*x_it].end();
	 y_x++) {
      double energy_z_y = 0.0f, energy_z_y_x = 0.0f; /* z(y), z(y|x)のエネルギー関数値 */
      for (f_it = features.begin();
	   f_it != features.end();
	   f_it++) {
	if (f_it->is_marginal
	    && f_it->get_pattern_y() == *y_x) {
	  energy_z_y += f_it->parameter * f_it->weight;
	} else if (!f_it->is_marginal) {
	  energy_z_y_x += f_it->checkget_param_weight(*x_it, *y_x);
	}
      }
      /* z(y|x) - z(y) の加算 */
      norm_factor[*x_it] += exp(energy_z_y) * ( exp(energy_z_y_x) - 1 );
    }
  }
  
}

/* モデルの確率分布・モデル期待値の素性へのセット */
void MEModel::calc_model_prob(void)
{
  std::vector<MEFeature>::iterator f_it;           /* 素性イテレータ */
  std::set<std::vector<int> >::iterator x_it;      /* 集合Xのイテレータ */
  std::vector<int> pattern_xy, pattern_x;          /* xyの順に連結したパターンとxのパターン */

  /* まず, 正規化項Z(x), Zの計算 */
  calc_normalized_factor();
  
  /* モデルの確率分布の計算 */
  for (x_it = setX.begin(); x_it != setX.end(); x_it++) {
    for (int word_y = 0; word_y < unique_word_no; word_y++) {
      /* 確率分布にセットするパターンの生成
	 xyの順にパターンを連結 */
      pattern_xy.resize((*x_it).size()+1);
      std::copy((*x_it).begin(), (*x_it).end(), pattern_xy.begin());
      pattern_xy.push_back(word_y);
      /* エネルギー関数値の計算 */
      double energy = 0.0f;
      for (f_it = features.begin();
	   f_it != features.end();
	   f_it++) {
	energy += f_it->checkget_param_weight(*x_it, word_y);
      }
      /* 結合確率・条件付き確率のセット */
      cond_prob[pattern_xy]  = exp(energy) / norm_factor[*x_it];
      joint_prob[pattern_xy] = exp(energy) / joint_norm_factor;
    }
  }

  /* 期待値のリセット */
  for (f_it = features.begin();
       f_it != features.end();
       f_it++) {
    f_it->model_E = 0.0f;
  }

  /* モデル期待値の素性へのセット. 経験確率の時と同様の手法.
     TODO:間違っとる. xについての和は取れない. 近似して経験分布P~(x)を使って計算しましょう */
  for (f_it = features.begin();
       f_it != features.end();
       f_it++) {
    for (x_it = setX.begin();
	 x_it != setX.end();
	 x_it++) {
      for (int word_y = 0; word_y < unique_word_no; word_y++) {
	/* 確率分布から取り出すパターンの生成 */
	pattern_xy.resize((*x_it).size()+1);
	std::copy((*x_it).begin(), (*x_it).end(), pattern_xy.begin());
	pattern_xy.push_back(word_y);
	f_it->model_E
	  += f_it->checkget_weight(*x_it, word_y) * joint_prob[pattern_xy];
      }
    }
  }
         
}

/* 周辺素性を活性化させるyの集合Ymと, 
   条件付き素性を活性化させるyの集合Y(x)をセットする. */
void MEModel::set_setY(void)
{
  std::vector<MEFeature>::iterator f_it;
  std::set<std::vector<int> >::iterator x_it;

  /* Ymのセット : Y（単語）の要素を走査し, 周辺素性が活性化される要素を集める */
  for (int word_y = 0; word_y < unique_word_no; word_y++) {
    for (f_it = features.begin();
        f_it != features.end();
        f_it++) {
      if (f_it->is_marginal 
          && word_y == f_it->get_pattern_y()) {
        /* 周辺素性を活性化させているYの要素（単語）を発見 
           -> 集合に追加. breakして次の単語のチェックに */
        setY_marginal.insert(word_y);
        break;
      }
    }
  }

  /* Y(x)のセット : 一番外側でXの要素（パターン）を走査, その中で条件付き素性が活性化される要素を集める */
  for (x_it = setX.begin();
       x_it != setX.end();
       x_it++) {
    for (int word_y = 0; word_y < unique_word_no; word_y++) {
      for (f_it = features.begin();
           f_it != features.end();
           f_it++) {
        if (!f_it->is_marginal
            && f_it->check_pattern(*x_it, word_y)) {
          /* 条件付き素性かつ,
             その素性を活性化させる組み合わせを発見
             -> 集合に追加し, breakして次のYの要素（単語）へ */
          setY_cond[*x_it].insert(word_y);
          break;
        }
      }
    }
  }	  

}

/* 候補素性情報の印字 */
void MEModel::print_candidate_features_info(void)
{
  std::vector<MEFeature>::iterator f_it;     /* 素性のイテレータ */  

  std::cout << "******** Feature's info ********" << std::endl;
  for (f_it = candidate_features.begin();
       f_it != candidate_features.end();
       f_it++) {
    f_it->print_info();
    std::cout << "---------------------------------------" << std::endl;
  }
  std::cout << "There are " << candidate_features.size() << " candidate features." << std::endl;
}

/* モデル素性情報の印字 */
void MEModel::print_model_features_info(void)
{
  std::vector<MEFeature>::iterator f_it;     /* 素性のイテレータ */  

  std::cout << "******** Feature's info ********" << std::endl;
  for (f_it = features.begin();
       f_it != features.end();
       f_it++) {
    f_it->print_info();
    std::cout << "---------------------------------------" << std::endl;
  }
  std::cout << "There are " << features.size() << " model features." << std::endl;

}  

/* (テスト用;for debug)候補素性をモデル素性にコピーする */
void MEModel::copy_candidate_features_to_model_features(void)
{
  features.resize(candidate_features.size());
  std::copy(candidate_features.begin(), candidate_features.end(),
            features.begin());
}

